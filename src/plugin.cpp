#include "Contract.h"
#include "Physics.h"
#include "Scene3D.h"
#include "DevBenchAPI.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <windows.h>

namespace {
using observer::json;
using Clock = std::chrono::steady_clock;
std::int64_t Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }
const std::string session = std::format("{}-{}", GetCurrentProcessId(), Now());
std::atomic<std::uint64_t> generation{0}, sample{0};
std::atomic<int> pending{0};
std::atomic<bool> loading{true};
DevBenchAPI::IDevBenchInterface001* api = nullptr;

bool InitialWorldReadable() {
    const auto ui = RE::UI::GetSingleton();
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto cell = player ? player->GetParentCell() : nullptr;
    if (!ui || !player || !cell) return false;
    const auto editor = cell->GetFormEditorID();
    const bool playroom = !editor || std::strcmp(editor, "VRPlayroom01") == 0;
    const bool blocked = ui->IsMenuOpen("Main Menu") || ui->IsMenuOpen("Loading Menu") ||
                         ui->IsMenuOpen("RaceSex Menu") || ui->IsMenuOpen("CalibrationOptionMenu") ||
                         ui->IsMenuOpen("MessageBoxMenu") || ui->IsMenuOpen("Fader Menu");
    return observer::InitialWorldReadable(generation.load(), loading.load(),
        observer::SceneRoot(player) != nullptr, cell->IsAttached(), playroom, ui->GameIsPaused(), blocked);
}

json Error(std::string reason, std::string outcome = "rejected") {
    return {{"schemaVersion",1},{"ok",false},{"sessionId",session},{"loadGeneration",generation.load()},
            {"error",std::move(reason)},{"outcome",std::move(outcome)}};
}
json Capabilities() {
    return {{"schemaVersion",1},{"ok",true},{"observerVersion","0.2.3"},{"sessionId",session},
        {"readOnly",true},{"phase","skse_main_thread_task"},
        {"domains",{{"references",true},{"nodes",true},{"vrPicking",true},{"physics",true},{"render",false}}},
        {"bounds",{{"refs",16},{"nodes",64},{"requestBytes",16384},{"pendingTasks",4},{"timeoutMs",{100,3000}}}},
        {"physics",{{"bodies",true},{"contactCallbacks",true},{"currentManifold",false},{"continuousContactCoverage",false},
                    {"bounds",{{"refs",16},{"bodies",64},{"sceneNodes",128},{"contactRing",256},{"worldLifetimeSubscriptions",32},{"leaseMs",5000}}}}},
        {"renderReason","No renderer provider"}};
}
json Vec(const RE::NiPoint3& v) { return json::array({v.x,v.y,v.z}); }
json Transform(const RE::NiTransform& t) {
    auto matrix = json::array();
    for (const auto& row : t.rotate.entry) for (float v : row) matrix.push_back(v);
    return {{"translation",Vec(t.translate)},{"rotationRowMajor",matrix},{"scale",t.scale}};
}
std::string Hex(std::uint32_t id) { return std::format("0x{:08X}",id); }
RE::TESObjectREFR* Ref(std::uint32_t id) {
    const auto form = RE::TESForm::LookupByID(id);
    return form ? form->As<RE::TESObjectREFR>() : nullptr;
}
json Identity(RE::TESObjectREFR* ref, std::uint64_t gen) {
    json out{{"form",Hex(ref->GetFormID())},{"loadGeneration",gen},
             {"runtimeHandle",ref->GetHandle().native_handle()}};
    const auto file = ref->GetFile(0);
    if (!ref->IsDynamicForm() && file) {
        out["sourcePlugin"] = file->fileName;
        out["localFormId"] = Hex(ref->GetLocalFormID());
    }
    return out;
}
json ReadVRPicking(std::uint64_t gen) {
    json out{{"status","available"},{"phase","skse_main_thread_task"},
        {"units","skyrim_engine_units"},{"space","world"},
        {"collisionPointValidity","raw engine field; not proof of a fresh hit"},
        {"devices",json::object()},{"nodes",json::object()}};
    const auto picks = RE::CrosshairPickData::GetSingleton();
    if (!picks) return {{"status","unavailable"},{"reason","VR pick data unavailable"}};
    // SkyrimVR1.4.15 holds three target handles from offset4 and three
    // collision points from offset0x28. The pinned3.7.0 flat SDK declaration
    // represents one device, so do not dereference its named scalar fields.
    std::array<std::uint32_t,3> handles{};
    std::array<RE::NiPoint3,3> points{};
    static_assert(sizeof(RE::NiPoint3)==12);
    const auto bytes = reinterpret_cast<const std::byte*>(picks);
    std::memcpy(handles.data(),bytes+4,sizeof(handles));
    std::memcpy(points.data(),bytes+0x28,sizeof(points));
    std::array<std::uint32_t,3> rechecked{};
    std::memcpy(rechecked.data(),bytes+4,sizeof(rechecked));
    if (handles!=rechecked) return {{"status","unavailable"},{"reason","VR target handles changed during read"}};
    constexpr std::array names{"left","right","headset"};
    for (std::size_t i=0;i<3;++i) {
        RE::NiPointer<RE::TESObjectREFR> ref;
        if (handles[i]) RE::LookupReferenceByHandle(handles[i],ref);
        json device{{"targetStatus",handles[i] ? (ref ? "available" : "unavailable") : "none"},
                    {"target",ref ? Identity(ref.get(),gen) : json(nullptr)},
                    {"collisionPoint",Vec(points[i])}};
        if (!observer::Finite(device)) device={{"targetStatus","unavailable"},{"reason","Non-finite VR picking data"}};
        out["devices"][names[i]]=std::move(device);
    }
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto nodes = player ? player->GetVRNodeData() : nullptr;
    if (nodes) {
        const auto add = [&](const char* role, RE::NiNode* node) {
            json value = node ? json{{"status","available"},{"world",Transform(node->world)},
                                     {"name",node->name.c_str() ? node->name.c_str() : ""}}
                              : json{{"status","unavailable"},{"reason","VR node missing"}};
            if (!observer::Finite(value)) value={{"status","unavailable"},{"reason","Non-finite VR node transform"}};
            out["nodes"][role]=std::move(value);
        };
        add("leftWand",nodes->LeftWandNode.get());
        add("rightWand",nodes->RightWandNode.get());
        add("uprightHmd",nodes->UprightHmdNode.get());
        add("primaryAim",nodes->PrimaryMagicAimNode.get());
    }
    // Exact VR layout: first context at0x60, BSTArray stride0x18; device3/4
    // are Vive primary/secondary. The older SDK enum names these differently.
    json bindings{{"status","unavailable"},{"reason","Gameplay control map unavailable"}};
    const auto controlMap = RE::ControlMap::GetSingleton();
    const auto context = controlMap ? controlMap->controlMap[0] : nullptr;
    if (context) {
        bindings={{"status","available"},{"context","gameplay"},{"devices",json::object()}};
        static_assert(sizeof(RE::BSTArray<RE::ControlMap::UserEventMapping>)==0x18);
        constexpr std::array deviceNames{"vivePrimary","viveSecondary"};
        constexpr std::array events{"Activate","Teleport Or Activate","Jump","Sneak Or Jump"};
        for (std::size_t role=0;role<2;++role) {
            const auto& mappings=context->deviceMappings[3+role];
            if (mappings.size()>256) {
                bindings={{"status","unavailable"},{"reason","Gameplay mapping bound exceeded"}}; break;
            }
            auto values=json::object();
            for (const char* event:events) {
                auto matches=json::array();
                for (const auto& mapping:mappings) {
                    if (mapping.eventID==event) matches.push_back({{"key",mapping.inputKey},
                        {"modifier",mapping.modifier},{"linked",mapping.linked}});
                }
                values[event]=std::move(matches);
            }
            bindings["devices"][deviceNames[role]]=std::move(values);
        }
    }
    out["gameplayBindings"]=std::move(bindings);
    return out;
}
json ReadRef(std::uint32_t id, std::uint64_t gen) {
    const auto ref = Ref(id);
    if (!ref) return {{"form",Hex(id)},{"status","unavailable"},{"reason","Reference not resolved"}};
    json out{{"identity",Identity(ref,gen)},{"status","available"},{"deleted",ref->IsDeleted()},
             {"disabled",ref->IsDisabled()},{"position",Vec(ref->GetPosition())},{"anglesRadians",Vec(ref->GetAngle())},
             {"scale",ref->GetScale()}};
    const auto base = ref->GetBaseObject();
    out["baseForm"] = base ? json(Hex(base->GetFormID())) : json(nullptr);
    const auto cell = ref->GetParentCell();
    out["cellForm"] = cell ? json(Hex(cell->GetFormID())) : json(nullptr);
    const auto root = observer::SceneRoot(ref);
    out["loaded3D"] = root != nullptr;
    out["sceneTransform"] = root ? Transform(root->world) : json(nullptr);
    if (!observer::Finite(out)) return {{"form",Hex(id)},{"status","unavailable"},{"reason","Non-finite reference transform"}};
    return out;
}
json ReadNode(const observer::NodeRequest& request, std::uint64_t gen) {
    json out{{"form",Hex(request.ref)},{"name",request.name},{"firstPerson",request.firstPerson}};
    const auto ref = Ref(request.ref);
    if (!ref || ref->IsDeleted()) { out.update({{"status","unavailable"},{"reason","Reference missing or deleted"}}); return out; }
    out["identity"] = Identity(ref,gen);
    const auto root = observer::SceneRoot(ref,request.firstPerson);
    if (!root) { out.update({{"status","unavailable"},{"reason","Requested 3D tree not loaded"}}); return out; }
    const auto node = root->GetObjectByName(RE::BSFixedString(request.name));
    if (!node) { out.update({{"status","unavailable"},{"reason","Node not found"}}); return out; }
    out.update({{"status","available"},{"world",Transform(node->world)},{"local",Transform(node->local)},
        {"worldBound",{{"center",Vec(node->worldBound.center)},{"radius",node->worldBound.radius}}},
        {"sceneCollisionObjectPresent",node->GetCollisionObject()!=nullptr}});
    if (!observer::Finite(out)) return {{"form",Hex(request.ref)},{"name",request.name},{"status","unavailable"},{"reason","Non-finite node transform"}};
    return out;
}
json Frame() {
    try {
        static REL::Relocation<std::int32_t*> counter{REL::RelocationID(525008,411489)};
        return *counter;
    } catch (...) { return nullptr; }
}
json Snapshot(const observer::Request& request, std::uint64_t gen) {
    const auto started = Now();
    json out{{"schemaVersion",1},{"observerVersion","0.2.3"},{"ok",true},
        {"sessionId",session},{"loadGeneration",gen},{"sampleId",++sample},
        {"producerFrame",Frame()},{"producerMonotonicNs",started},
        {"phase","skse_main_thread_task"},{"units","skyrim_engine_units"},{"space","world"},
        {"coherence",{{"mode","single_main_thread_task"},{"physics","not_sampled"},{"render","not_synchronized"}}},
        {"refs",json::array()},{"nodes",json::array()},
        {"physics",{{"status","unavailable"},{"reason","No safe phase collector"}}},
        {"render",{{"status","unavailable"},{"reason","No renderer provider"}}}};
    out["vrPicking"]=ReadVRPicking(gen);
    bool complete = true;
    for (auto id : request.refs) { auto value=ReadRef(id,gen); complete &= value["status"]=="available"; out["refs"].push_back(std::move(value)); }
    for (const auto& n : request.nodes) { auto value=ReadNode(n,gen); complete &= value["status"]=="available"; out["nodes"].push_back(std::move(value)); }
    if(!request.physicsRefs.empty()) {
        out["physics"]=observer::physics::Snapshot(request,gen,generation,loading);
        complete &= out["physics"]["status"]=="available";
        out["coherence"]["physics"]="separate_world_lock_and_asynchronous_contact_callbacks";
    }
    out["quality"] = complete ? "complete" : "partial";
    out["durationUs"] = (Now()-started)/1000;
    return out;
}
struct Job {
    observer::Gate gate;
    std::promise<json> promise;
    observer::Request request;
    std::uint64_t gen;
    Job(observer::Request r, std::uint64_t g) : gate(Now()+std::int64_t(r.timeoutMs)*1000000,g), request(std::move(r)), gen(g) {}
};
void Handle(void*,const char* text,void* sink,DevBenchAPI::WriteFn write) {
    json result;
    try {
        if (!text || strnlen_s(text,16385)>16384) throw std::invalid_argument("Request exceeds 16KiB");
        const auto args=json::parse(text);
        const auto action=args.value("action",std::string("snapshot"));
        if (action=="capabilities") result=Capabilities();
        else {
            if (action!="snapshot") throw std::invalid_argument("Unsupported action");
            auto request=observer::Parse(args);
            if (loading.load() && generation.load() != 0) result=Error("World loading or not yet initialized");
            else if (!SKSE::GetTaskInterface()) result=Error("SKSE tasks unavailable");
            else if (pending.fetch_add(1)>=4) { --pending; result=Error("Observer task capacity exceeded"); }
            else {
                struct Reservation {
                    bool transferred=false;
                    ~Reservation(){ if(!transferred) --pending; }
                } reservation;
                auto job=std::make_shared<Job>(std::move(request),generation.load());
                auto future=job->promise.get_future();
                SKSE::GetTaskInterface()->AddTask([job]() {
                    struct Finally { ~Finally(){ --pending; } } finally;
                    // Alternate starts can reach a real world without the hooked
                    // vanilla quest dispatching SKSE's NewGame message. Establish
                    // only the initial epoch from actual main-thread world state.
                    // Never recover a failed/ongoing load or rebind a stale job.
                    const bool initial = job->gen == 0 && InitialWorldReadable();
                    if (!job->gate.Begin(Now(),generation.load(),initial ? false : loading.load())) {
                        job->promise.set_value(Error("Queued observation expired or load generation changed","abandoned_before_start")); return;
                    }
                    if (initial) {
                        job->gen = ++generation;
                        loading.store(false);
                        observer::physics::Invalidate();
                        if (api) {
                            const json event{{"schemaVersion",1},{"sessionId",session},
                                {"loadGeneration",job->gen},{"loading",false},{"event","initialWorldObserved"},
                                {"basis","loaded_player3d_attached_cell_no_startup_menu"},{"producerMonotonicNs",Now()}};
                            api->EmitEvent("world_observer.lifecycle",event.dump().c_str());
                        }
                    }
                    try {
                        auto value=Snapshot(job->request,job->gen);
                        if (Now()>=job->gate.Deadline()) value=Error("Read completed after deadline","read_finished_after_deadline");
                        job->promise.set_value(std::move(value));
                    }
                    catch (const std::exception& e) { job->promise.set_value(Error(e.what(),"read_failed")); }
                    job->gate.Finish();
                });
                reservation.transferred=true;
                const auto deadline=Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(job->gate.Deadline())));
                if (future.wait_until(deadline)==std::future_status::ready) result=future.get();
                else { const bool canceled=job->gate.Cancel(); result=Error("Observation deadline elapsed",canceled?"abandoned_before_start":"read_started_result_unavailable"); }
            }
        }
    } catch (const std::exception& e) { result=Error(e.what()); }
    const auto output=result.dump();
    write(sink,output.c_str());
}
void Connect() {
    api=DevBenchAPI::GetDevBenchInterface001();
    if (!api || api->GetBuildNumber()<10500) { spdlog::warn("DevBench extension ABI unavailable"); return; }
    const json formSchema{{"oneOf",json::array({
        json{{"type","integer"},{"minimum",1},{"maximum",4294967295ULL}},
        json{{"type","string"},{"pattern","^0x[0-9a-fA-F]{1,8}$"}}})}};
    const json inputSchema{{"type","object"},{"additionalProperties",false},
        {"properties",{{"kind",{{"const","world_observer"}}},{"action",{{"enum",{"snapshot","capabilities"}}}},
            {"refs",{{"type","array"},{"maxItems",16},{"items",formSchema}}},
            {"nodes",{{"type","array"},{"maxItems",64},{"items",{{"type","object"},{"additionalProperties",false},
                {"required",{"ref","name"}},{"properties",{{"ref",formSchema},{"name",{{"type","string"},{"minLength",1},{"maxLength",128}}},
                    {"firstPerson",{{"type","boolean"}}}}}}}}},
            {"timeoutMs",{{"type","integer"},{"minimum",100},{"maximum",3000}}}}}};
    auto schema=inputSchema;
    schema["properties"]["physics"]={{"type","object"},{"additionalProperties",false},{"required",{"refs"}},
        {"properties",{{"refs",{{"type","array"},{"minItems",1},{"maxItems",16},{"items",formSchema}}},
                       {"afterSequence",{{"type","integer"},{"minimum",0}}}}}};
    const json descriptor{{"description","Read selected world reference/node state in one bounded main-thread task; no game mutation"},
        {"readOnly",true},{"inputSchema",schema},
        {"capabilities",Capabilities()}};
    api->RegisterToolExtension("inspect","world_observer",descriptor.dump().c_str(),Handle,nullptr);
    spdlog::info("Registered inspect world_observer, host build {}",api->GetBuildNumber());
}
void OnMessage(SKSE::MessagingInterface::Message* m) {
    if (!m) return;
    if (m->type==SKSE::MessagingInterface::kPostPostLoad) Connect();
    if (m->type==SKSE::MessagingInterface::kPreLoadGame || m->type==SKSE::MessagingInterface::kNewGame) { ++generation; loading.store(true); observer::physics::Invalidate(); }
    // SKSEVR encodes the load result in the pointer value, not a pointed-to bool.
    if (m->type==SKSE::MessagingInterface::kPostLoadGame) loading.store(m->data==nullptr);
    if (m->type==SKSE::MessagingInterface::kNewGame) loading.store(false);
    if (api && (m->type==SKSE::MessagingInterface::kPreLoadGame || m->type==SKSE::MessagingInterface::kPostLoadGame || m->type==SKSE::MessagingInterface::kNewGame)) {
        json event{{"schemaVersion",1},{"sessionId",session},{"loadGeneration",generation.load()},
            {"loading",loading.load()},{"skseMessage",m->type},{"producerMonotonicNs",Now()}};
        if(m->type==SKSE::MessagingInterface::kPostLoadGame) event["loadSucceeded"]=m->data!=nullptr;
        api->EmitEvent("world_observer.lifecycle",event.dump().c_str());
    }
}
}
extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* skse,SKSE::PluginInfo* info) {
    info->infoVersion=SKSE::PluginInfo::kVersion; info->name="SkyrimWorldObserver"; info->version=1;
    // SKSEVR reports 1.4.15.1 (0x010400F1), while the executable/CommonLib
    // version is 1.4.15.0. Accept the two encodings of this supported runtime.
    const auto version=skse->RuntimeVersion();
    return !skse->IsEditor() && version[0]==1 && version[1]==4 && version[2]==15 && version[3]<=1;
}
extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    if (auto dir=SKSE::log::log_directory()) {
        *dir/="SkyrimWorldObserver.log";
        auto log=std::make_shared<spdlog::logger>("world_observer",std::make_shared<spdlog::sinks::basic_file_sink_mt>(dir->string(),true));
        spdlog::set_default_logger(log); spdlog::flush_on(spdlog::level::info);
    }
    auto messaging=SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) return false;
    spdlog::info("World observer 0.2.3 loaded, session {}",session);
    return true;
}
