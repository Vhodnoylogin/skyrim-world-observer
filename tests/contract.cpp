#include "Contract.h"
#include "PhysicsCache.h"
#include "NativeLockValidation.h"
#include <iostream>
#include <limits>
using observer::json;
void Require(bool v) { if(!v) throw std::runtime_error("Contract check failed"); }
template<class F> void Reject(F fn) { bool rejected=false; try { fn(); } catch(const std::exception&) { rejected=true; } Require(rejected); }
int main() {
    Require(observer::Form("0x14")==20);
    Reject([]{observer::Form(-1);}); Reject([]{observer::Form(0);}); Reject([]{observer::Form("0x14garbage");});
    Reject([]{observer::Form(1.5);}); Reject([]{observer::Form(std::uint64_t(UINT32_MAX)+1);});
    auto r=observer::Parse(json{{"refs",{"0x14"}},{"nodes",{{{"ref","0x14"},{"name","Hand"},{"firstPerson",true}}}}});
    Require(r.refs.size()==1 && r.nodes[0].firstPerson);
    Reject([]{observer::Parse(json{{"refs",json(std::vector<int>(17,20))}});});
    Reject([]{observer::Parse(json{{"timeoutMs",4000}});});
    Reject([]{observer::Parse(json{{"timeoutMs",std::uint64_t(UINT32_MAX)+101}});});
    Reject([]{observer::Parse(json{{"nodes",{{{"ref",20},{"name","Hand"},{"firstPerson",1}}}}});});
    Reject([]{observer::Parse(json{{"physics",true}});});
    auto p=observer::Parse(json{{"physics",{{"refs",{"0xFF000001"}},{"afterSequence",5}}}});
    Require(p.physicsRefs.size()==1 && p.afterSequence==5);
    Reject([]{observer::Parse(json{{"physics",{{"refs",json::array()}}}});});
    Reject([]{observer::Parse(json{{"physics",{{"refs",{20}},{"afterSequence",-1}}}});});
    Reject([]{observer::Parse(json{{"physics",{{"refs",{20}},{"mutate",true}}}});});
    observer::physics::Cache<2> cache;
    auto bounded=observer::Parse(json{{"actorState",true},{"physics",{{"refs",{20}},
        {"captureAction","start"},{"captureId",std::string(32,'a')},{"captureWindowMs",60000},{"maximumSamples",256}}}});
    Require(bounded.actorState && bounded.captureWindowNs==60000000000LL);
    Reject([]{observer::Parse(json{{"physics",{{"refs",{20}},{"captureAction","read"},{"captureId",std::string(32,'a')},{"captureWindowMs",1}}}});});
    Reject([]{observer::Parse(json{{"physics",{{"refs",{20}},{"captureAction","start"},{"captureId",std::string(32,'a')},{"captureWindowMs",60001}}}});});
    Reject([]{observer::Parse(json{{"actorState",1}});});
    Reject([]{observer::Parse(json{{"physics",{{"refs",{20}},{"maximumSamples",true}}}});});
    const std::array<std::uint32_t,1> watched{42};
    cache.Arm(watched,2,100);
    Require(cache.Interested(42,7,2,101));
    Require(!cache.Interested(7,8,2,101));
    Require(!cache.Interested(42,7,3,101));
    Require(!cache.Interested(42,7,2,5000000100LL));
    observer::physics::Contact contact;contact.generation=2;contact.world=1;contact.timeNs=101;
    cache.Push(contact);cache.Push(contact);cache.Push(contact);
    Require(cache.Gap(0) && !cache.Gap(1));
    Require(!cache.Gap(UINT64_MAX));
    Require(cache.Since(0,2,1).size()==2);
    Require(cache.Since(0,3,1).empty() && cache.Since(0,2,2).empty());
    cache.Arm(watched,3,102);Require(cache.Since(0,3,1).empty());
    const auto epoch=cache.epoch;cache.Arm(watched,3,103);Require(cache.epoch==epoch);
    const std::array<std::uint32_t,1> changed{43};cache.Arm(changed,3,104);
    Require(cache.epoch>epoch && !cache.Interested(42,7,3,105) && cache.Interested(43,7,3,105));
    cache.Invalidate();Require(!cache.Interested(42,7,3,103));
    cache.StartBounded(watched,4,1000,60000000000LL);
    const auto fixedEnd=cache.untilNs;const auto fixedEpoch=cache.epoch;
    Require(cache.SameBodies(watched) && !cache.SameBodies(changed));
    Require(cache.Interested(42,7,4,fixedEnd-1) && !cache.Interested(42,7,4,fixedEnd));
    Require(!cache.Interested(42,7,4,999));
    contact.generation=4;contact.timeNs=1001;cache.Push(contact);
    contact.timeNs=fixedEnd;cache.Push(contact);
    Require(cache.Since(0,4,1).size()==1);
    cache.Since(0,4,1);Require(cache.untilNs==fixedEnd && cache.epoch==fixedEpoch);
    Reject([&]{cache.StartBounded(watched,4,1000,60000000001LL);});
    using observer::physics::LockWords;
    auto readLock=[](LockWords* l){++l->count;return true;};
    auto writeLock=[](LockWords* l){if(l->writer==42)++l->count;else{l->writer=42;l->count=0x80000001u;}return true;};
    auto unRead=[](LockWords* l){--l->count;};
    auto unWrite=[](LockWords* l){if(l->count-1==0x80000000u){l->writer=0;l->count=0;}else --l->count;};
    Require(observer::physics::ValidateNativeLocks(readLock,writeLock,unRead,unWrite,42));
    // Reject the exact reviewed defect: writer flag without native count1.
    auto defective=[](LockWords* l){l->writer=42;l->count=0x80000000u;return true;};
    Require(!observer::physics::ValidateNativeLocks(readLock,defective,unRead,unWrite,42));
    Require(observer::InitialWorldReadable(0,true,true,true,false,false,false));
    Require(!observer::InitialWorldReadable(1,true,true,true,false,false,false));
    Require(!observer::InitialWorldReadable(0,true,false,true,false,false,false));
    Require(!observer::InitialWorldReadable(0,true,true,false,false,false,false));
    Require(!observer::InitialWorldReadable(0,true,true,true,true,false,false));
    Require(!observer::InitialWorldReadable(0,true,true,true,false,true,false));
    Require(!observer::InitialWorldReadable(0,true,true,true,false,false,true));
    observer::Gate expired(100,2); Require(!expired.Begin(100,2,false));
    observer::Gate stale(100,2); Require(!stale.Begin(50,3,false));
    observer::Gate loading(100,2); Require(!loading.Begin(50,2,true));
    observer::Gate canceled(100,2); Require(canceled.Cancel()); Require(!canceled.Begin(50,2,false));
    observer::Gate running(100,2); Require(running.Begin(50,2,false)); Require(!running.Cancel()); running.Finish(); Require(running.Get()==observer::State::finished);
    Require(!observer::Finite(json{{"position",{std::numeric_limits<double>::infinity()}}}));
    std::cout << "Observer request bounds, identity, stale-load and deadline checks passed\n";
}
