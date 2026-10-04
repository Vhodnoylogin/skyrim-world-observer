#include "Physics.h"
#include "PhysicsCache.h"
#include "NativeLockValidation.h"
#include "Scene3D.h"
#include <RE/Skyrim.h>
#include <RE/H/hkpContactListener.h>
#include <RE/H/hkpContactPointEvent.h>
#include <RE/H/hkContactPoint.h>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <windows.h>
#include <bcrypt.h>

namespace observer::physics {
namespace {
std::int64_t Now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
json Vec(const RE::hkVector4& v){return json::array({v.quad.m128_f32[0],v.quad.m128_f32[1],v.quad.m128_f32[2]});}
std::array<float,3> Copy(const RE::hkVector4& v){return {v.quad.m128_f32[0],v.quad.m128_f32[1],v.quad.m128_f32[2]};}
std::string Hex(std::uint32_t n){return std::format("0x{:08X}",n);}

bool Fingerprint(std::uintptr_t address,std::size_t size,std::string_view expected){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    DWORD objectSize=0,written=0;
    bool good=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&written,0)>=0;
    std::vector<UCHAR> storage(objectSize);std::array<UCHAR,32> digest{};
    if(good)good=BCryptCreateHash(algorithm,&hash,storage.data(),objectSize,nullptr,0,0)>=0;
    if(good)good=BCryptHashData(hash,reinterpret_cast<PUCHAR>(address),static_cast<ULONG>(size),0)>=0;
    if(good)good=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    if(!good)return false;
    std::string text;for(auto b:digest)text+=std::format("{:02x}",b);
    return text==expected;
}
using TryLock=bool(*)(void*);
using Unlock=void(*)(void*);
std::uintptr_t Base(){return REL::Module::get().base();}
TryLock TryRead(){return reinterpret_cast<TryLock>(Base()+0xC42350);}
TryLock TryWrite(){return reinterpret_cast<TryLock>(Base()+0xC423C0);}
Unlock UnRead(){return reinterpret_cast<Unlock>(Base()+0xC42410);}
Unlock UnWrite(){return reinterpret_cast<Unlock>(Base()+0xC42420);}
bool ABIQualified(){
    static const bool qualified=[](){
        const auto base=Base();
        if(!Fingerprint(base+0xC42350,93,"8b4cdb2416b5132351c1fd887677b7938bfc36e1aae48e28b6e20104c4ea4bbe") ||
           !Fingerprint(base+0xC423C0,63,"e0bff32fd21abba083c9818818ad744abb19e50a6148e846e0a1b7e4d29db82d") ||
           !Fingerprint(base+0xC42410,5,"95cd34517f0adc8f15b6acbafe7003341718f9b68707cf93365922bbd73c055b") ||
           !Fingerprint(base+0xC42420,28,"1cc1401271e61d82c962469fb85fe754233200d32d73bdcdfbdebe22f6cac22c"))return false;
        return ValidateNativeLocks(TryRead(),TryWrite(),UnRead(),UnWrite(),::GetCurrentThreadId());
    }();
    return qualified;
}

// Native nonblocking try-locks, including the engine's ownership and memory
// fences. Never recreate the writer protocol: its held count is 0x80000001,
// not merely the writer flag. Exact loaded-code fingerprints fail closed.
class WorldLock {
public:
    WorldLock(RE::BSReadWriteLock& lock,bool write):lock_(lock),write_(write){
        held_=ABIQualified() && (write_?TryWrite():TryRead())(&lock_);
    }
    ~WorldLock(){if(held_)(write_?UnWrite():UnRead())(&lock_);}
    explicit operator bool()const{return held_;}
    WorldLock(const WorldLock&)=delete;
private: RE::BSReadWriteLock& lock_;bool write_,held_=false;
};

class Listener final:public RE::hkpContactListener {
public:
    explicit Listener(std::uint64_t id):worldId(id){}
    void ContactPointCallback(const RE::hkpContactPointEvent& e) override {
        const auto now=Now();
        if(now>=until.load(std::memory_order_relaxed) || !gen || !loading || loading->load())return;
        // These pointers are owned by Havok for the duration of this callback.
        // No TESForm lookup, scene access, engine lock, JSON or network call here.
        if(!e.bodies[0] || !e.bodies[1] || !e.contactPoint)return;
        std::unique_lock guard(mutex,std::try_to_lock);
        if(!guard.owns_lock()){++busyDrops;return;}
        const auto g=gen->load();
        const auto a=e.bodies[0]->uid,b=e.bodies[1]->uid;
        if(!cache.Interested(a,b,g,now))return;
        Contact c;c.generation=g;c.world=worldId;c.timeNs=now;c.bodyA=a;c.bodyB=b;
        c.position=Copy(e.contactPoint->position);c.normal=Copy(e.contactPoint->separatingNormal);
        c.separation=e.contactPoint->separatingNormal.quad.m128_f32[3];
        c.callbackType=static_cast<int>(e.type);
        if(e.separatingVelocity){c.separatingVelocity=*e.separatingVelocity;c.velocityAvailable=true;}
        if(e.contactPointProperties){
            // Havok hkpSolverResults(8) + hkContactPointMaterial flags(+0x0B).
            // Layout source and supported runtime are recorded in docs/physics.md.
            std::memcpy(&c.flags,reinterpret_cast<const std::byte*>(e.contactPointProperties)+0x13,1);
            c.flagsAvailable=true;
        }
        cache.Push(c);
    }
    std::uint64_t worldId;
    std::mutex mutex;
    Cache<> cache;
    std::atomic<std::uint64_t> busyDrops{0};
    std::atomic<std::int64_t> until{0};
    const std::atomic<std::uint64_t>* gen=nullptr;
    const std::atomic<bool>* loading=nullptr;
};
// Deliberately process lifetime: a deleted world drops its listener array. It
// never owns/deletes the callback object. No old-world pointer is retained or
// dereferenced. Revisited live worlds are identified by their current array.
// The fixed cap bounds memory across arbitrary cell/load churn.
std::array<Listener*,32> listeners{};
std::size_t listenerCount=0;

Listener* Find(RE::hkpWorld* world){
    for(auto p:world->contactListeners)for(std::size_t i=0;i<listenerCount;++i)if(p==listeners[i])return listeners[i];
    return nullptr;
}
struct Body {std::uint32_t form;RE::hkpRigidBody* body;std::string node;};
struct BodyDiagnostics {
    unsigned nodes=0,collisions=0,unsupportedCollision=0,missingWrapper=0,unsupportedBody=0,missingHavokBody=0;
    json Value(bool loaded)const{return {{"loaded3D",loaded},{"nodesVisited",nodes},{"collisionObjects",collisions},
        {"unsupportedCollisionTypes",unsupportedCollision},{"missingBodyWrapper",missingWrapper},
        {"unsupportedBodyTypes",unsupportedBody},{"missingHavokObject",missingHavokBody}};}
};
void Gather(RE::NiAVObject* node,std::uint32_t form,std::vector<Body>& out,std::unordered_set<RE::hkpRigidBody*>& seen,
            unsigned& visited,bool& truncated,BodyDiagnostics& diagnostics) {
    if(!node)return;
    if(visited++>=128 || out.size()>=64){truncated=true;return;}
    ++diagnostics.nodes;
    // HIGGS's established VR path uses the actual +0x40 collision member and
    // engine RTTI, rather than assuming virtual AsBhk... helpers are populated.
    if(auto col=node->collisionObject.get()) {
        ++diagnostics.collisions;
        if(auto hkcol=skyrim_cast<RE::bhkCollisionObject*>(col)) {
            if(auto worldObject=hkcol->body.get()) {
                if(auto wrapper=skyrim_cast<RE::bhkRigidBody*>(worldObject)) {
                    if(auto raw=wrapper->referencedObject.get()) {
                        auto body=static_cast<RE::hkpRigidBody*>(raw);
                        if(seen.insert(body).second)out.push_back({form,body,node->name.c_str()});
                    }else ++diagnostics.missingHavokBody;
                }else ++diagnostics.unsupportedBody;
            }else ++diagnostics.missingWrapper;
        }else ++diagnostics.unsupportedCollision;
    }
    if(auto branch=node->AsNode())for(const auto& child:branch->GetChildren()) {
        if(visited>=128 || out.size()>=64){truncated=true;break;}
        Gather(child.get(),form,out,seen,visited,truncated,diagnostics);
    }
}
json Unavailable(std::string why){return {{"status","unavailable"},{"reason",why},{"absenceProven",false}};}
}

void Invalidate(){for(std::size_t i=0;i<listenerCount;++i)listeners[i]->until.store(0);}

json Snapshot(const Request& request,std::uint64_t generation,const std::atomic<std::uint64_t>& gen,const std::atomic<bool>& loading){
    if(request.physicsRefs.empty())return Unavailable("Physics not requested");
    if(!ABIQualified())return Unavailable("Native physics lock ABI fingerprint/qualification mismatch");
    RE::bhkWorld* wrapper=nullptr;
    json refs=json::array();
    for(auto id:request.physicsRefs){
        const auto f=RE::TESForm::LookupByID(id);const auto ref=f?f->As<RE::TESObjectREFR>():nullptr;
        if(!ref || ref->IsDeleted())return Unavailable("Selected physics reference missing or deleted");
        const auto cell=ref?ref->GetParentCell():nullptr;
        const auto w=cell?cell->GetbhkWorld():nullptr;
        if(!w || (wrapper && wrapper!=w))return Unavailable("Selected refs must be loaded in one physics world");
        wrapper=w;
    }
    if(!wrapper)return Unavailable("No physics world");
    auto world=wrapper->GetWorld1();if(!world)return Unavailable("Havok world unavailable");
    Listener* listener=nullptr;
    {
        WorldLock lock(wrapper->worldLock,false);if(!lock)return Unavailable("Physics world busy; retry");
        listener=Find(world);
    }
    if(!listener){
        WorldLock lock(wrapper->worldLock,true);if(!lock)return Unavailable("Physics subscription world busy; retry");
        listener=Find(world);
        if(!listener){
            if(listenerCount>=listeners.size())return Unavailable("Physics world lifetime capacity exceeded");
            listener=new Listener(listenerCount+1);listener->gen=&gen;listener->loading=&loading;
            world->contactListeners.reserve((std::max)(4,world->contactListeners.size()+1));
            world->contactListeners.push_back(listener);listeners[listenerCount++]=listener;
        }
    }
    json bodies=json::array();bool truncated=false;
    std::vector<std::uint32_t> uids;
    {
        WorldLock lock(wrapper->worldLock,false);if(!lock)return Unavailable("Physics world busy; retry");
        std::vector<Body> found;std::unordered_set<RE::hkpRigidBody*> seen;unsigned visited=0;
        for(auto id:request.physicsRefs){
            const auto f=RE::TESForm::LookupByID(id);const auto ref=f?f->As<RE::TESObjectREFR>():nullptr;
            const auto default3D=ref?ref->Get3D():nullptr;
            const auto thirdPerson3D=ref?ref->Get3D(false):nullptr;
            const auto loaded3D=ref && ref->loadedData?ref->loadedData->data3D.get():nullptr;
            // Both pinned CommonLib3.7 and SKSEVR2.0.12 assert these +0x68
            // loaded-state/node offsets. Avoid depending on a VR nonactor
            // virtual getter returning the same tree as the actual loaded data.
            const auto root=observer::SceneRoot(ref);
            BodyDiagnostics diagnostics;
            const auto start=found.size();Gather(root,id,found,seen,visited,truncated,diagnostics);
            auto detail=diagnostics.Value(root!=nullptr);
            detail.update({{"defaultGet3D",default3D!=nullptr},{"explicitThirdPersonGet3D",thirdPerson3D!=nullptr},
                {"loadedDataPresent",ref && ref->loadedData!=nullptr},{"loadedData3DPresent",loaded3D!=nullptr},
                {"treeSource",loaded3D?"loaded_data":"virtual_third_person"},
                {"formType",ref?json(static_cast<unsigned>(ref->GetFormType())):json(nullptr)},
                {"deleted",ref?json(ref->IsDeleted()):json(nullptr)},{"disabled",ref?json(ref->IsDisabled()):json(nullptr)}});
            refs.push_back({{"form",Hex(id)},{"status",found.size()>start?"available":"unavailable"},
                           {"diagnostics",detail},
                           {"bodyCount",found.size()-start},{"reason",found.size()>start?json(nullptr):json("No loaded rigid body; character proxy/phantom unsupported")}});
        }
        for(const auto& b:found){
            if(b.body->world!=world){truncated=true;continue;}
            const auto& m=b.body->motion;
            json value{{"form",Hex(b.form)},{"node",b.node},{"worldId",listener->worldId},{"bodyUid",b.body->uid},
                {"motionType",m.type.underlying()},{"linearVelocity",Vec(m.linearVelocity)},{"angularVelocity",Vec(m.angularVelocity)},
                {"massInverse",m.inertiaAndMassInv.quad.m128_f32[3]},
                {"active",b.body->simulationIsland?json(b.body->simulationIsland->isInActiveIslandsArray!=0):json(nullptr)},
                {"centerOfMass",Vec(m.motionState.sweptTransform.centerOfMass1)},
                {"status","available"}};
            if(!Finite(value))value={{"form",Hex(b.form)},{"status","unavailable"},{"reason","Non-finite rigid body state"}};
            else uids.push_back(b.body->uid);
            bodies.push_back(std::move(value));
        }
    }
    const auto now=Now();json contacts=json::array(),coverage;
    {
        std::lock_guard guard(listener->mutex);
        listener->cache.Arm(uids,generation,now);listener->until.store(listener->cache.untilNs);
        for(const auto& c:listener->cache.Since(request.afterSequence,generation,listener->worldId)){
            bool selected=false;for(auto uid:uids)selected|=uid==c.bodyA || uid==c.bodyB;if(!selected)continue;
            json value{{"sequence",c.sequence},{"producerMonotonicNs",c.timeNs},{"worldId",c.world},
                {"bodyA",c.bodyA},{"bodyB",c.bodyB},{"position",c.position},{"normalBtoA",c.normal},
                {"signedSeparation",c.separation},{"separatingVelocity",c.velocityAvailable?json(c.separatingVelocity):json(nullptr)},
                {"disabled",c.flagsAvailable?json((c.flags&8)!=0):json(nullptr)},
                {"newContact",c.flagsAvailable?json((c.flags&1)!=0):json(nullptr)},
                {"speculative",c.separation>0},{"callbackType",c.callbackType},{"phase","havok_contact_point_callback"},
                {"solverUsed",nullptr},{"potential",nullptr},{"finalSolverContactProven",false},
                {"status","available"}};
            if(!Finite(value))value={{"sequence",c.sequence},{"status","unavailable"},{"reason","Non-finite contact data"}};
            contacts.push_back(std::move(value));
        }
        coverage={{"mode","callback_events_since_subscription"},{"armedFromNs",listener->cache.startNs},
            {"armedUntilNs",listener->cache.untilNs},{"subscriptionEpoch",listener->cache.epoch},
            {"latestSequence",listener->cache.latest},{"oldestRetainedSequence",listener->cache.Oldest()},
            {"ringGap",listener->cache.Gap(request.afterSequence)},{"callbackBusyDrops",listener->busyDrops.load()},
            {"cursorAhead",request.afterSequence>listener->cache.latest},{"callbackBusyDropsScope","listener_process_lifetime"},
            {"continuousCallbacks",false},{"absenceProven",false},
            {"reason","Engine callback delays/disabled contacts/sleeping bodies may suppress callbacks; empty events do not prove absence"}};
    }
    bool complete=!truncated;for(const auto& ref:refs)complete&=ref["status"]=="available";
    for(const auto& b:bodies)complete&=b["status"]=="available";
    return {{"status",complete?"available":"partial"},{"worldId",listener->worldId},{"sampleMonotonicNs",now},
        {"bodyPhase","skse_main_thread_try_read_locked_world"},{"contactPhase","havok_contact_point_callback"},
        {"units",{{"position","havok_world_units"},{"linearVelocity","havok_world_units_per_second"},{"angularVelocity","radians_per_second"}}},
        {"coherence","body_state_one_world_read_lock_contacts_asynchronous"},{"refs",refs},{"bodies",bodies},
        {"quality",{{"bodies",complete?"complete":"partial"},{"contacts","sampled_callback_coverage"}}},
        {"truncated",truncated},{"contacts",contacts},{"coverage",coverage},
        {"currentManifoldAvailable",false},{"constraintsAndCharacterProxiesAvailable",false}};
}
}
