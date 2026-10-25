#include "SEContract.h"
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
    const bool blocked = ui->IsMenuOpen("Main Menu") || ui->IsMenuOpen("Loading Menu") ||
                         ui->IsMenuOpen("RaceSex Menu") || ui->IsMenuOpen("CalibrationOptionMenu") ||
                         ui->IsMenuOpen("MessageBoxMenu") || ui->IsMenuOpen("Fader Menu");
    return observer::InitialWorldReadable(generation.load(), loading.load(),
        player->Get3D(false) != nullptr, cell->IsAttached(), false, ui->GameIsPaused(), blocked);
}

json Error(std::string reason, std::string outcome = "rejected") {
    return {{"schemaVersion",1},{"ok",false},{"sessionId",session},{"loadGeneration",generation.load()},
            {"error",std::move(reason)},{"outcome",std::move(outcome)}};
}
json Capabilities() {
    return {{"schemaVersion",1},{"ok",true},{"observerVersion","0.3.0-se104"},{"sessionId",session},
        {"readOnly",true},{"phase","skse_main_thread_task"},{"runtime","SkyrimSE1.7.104.0"},
        {"domains",{{"references",true},{"nodes",true},{"actorState",false},{"vrPicking",false},{"physics",false},{"render",false}}},
        {"bounds",{{"refs",16},{"nodes",64},{"requestBytes",16384},{"pendingTasks",4},{"timeoutMs",{100,3000}}}},
        {"producerFrameAvailable",false},{"producerFrameReason","No frame relocation qualified for this SE subset"},
        {"physicsReason","SE subset contains no physics collector"},{"renderReason","No renderer provider"}};
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
json ReadRef(std::uint32_t id, std::uint64_t gen,bool actorState=false) {
    const auto ref = Ref(id);
    if (!ref) return {{"form",Hex(id)},{"status","unavailable"},{"reason","Reference not resolved"}};
    json out{{"identity",Identity(ref,gen)},{"status","available"},{"deleted",ref->IsDeleted()},
             {"disabled",ref->IsDisabled()},{"position",Vec(ref->GetPosition())},{"anglesRadians",Vec(ref->GetAngle())},
             {"scale",ref->GetScale()}};
    const auto base = ref->GetBaseObject();
    out["baseForm"] = base ? json(Hex(base->GetFormID())) : json(nullptr);
    const auto cell = ref->GetParentCell();
    out["cellForm"] = cell ? json(Hex(cell->GetFormID())) : json(nullptr);
    const auto root = ref->Get3D(false);
    out["loaded3D"] = root != nullptr;
    out["sceneTransform"] = root ? Transform(root->world) : json(nullptr);
    if(actorState) out["actorState"]={{"status","unavailable"},{"reason","SE104 subset does not implement actorState"}};
    if (!observer::Finite(out)) return {{"form",Hex(id)},{"status","unavailable"},{"reason","Non-finite reference transform"}};
    return out;
}
json ReadNode(const observer::NodeRequest& request, std::uint64_t gen) {
    json out{{"form",Hex(request.ref)},{"name",request.name},{"firstPerson",request.firstPerson}};
    const auto ref = Ref(request.ref);
    if (!ref || ref->IsDeleted()) { out.update({{"status","unavailable"},{"reason","Reference missing or deleted"}}); return out; }
    out["identity"] = Identity(ref,gen);
    const auto root = ref->Get3D(request.firstPerson);
    if (!root) { out.update({{"status","unavailable"},{"reason","Requested 3D tree not loaded"}}); return out; }
    const auto node = root->GetObjectByName(RE::BSFixedString(request.name));
    if (!node) { out.update({{"status","unavailable"},{"reason","Node not found"}}); return out; }
    out.update({{"status","available"},{"world",Transform(node->world)},{"local",Transform(node->local)},
        {"worldBound",{{"center",Vec(node->worldBound.center)},{"radius",node->worldBound.radius}}},
        {"sceneCollisionObjectPresent",node->GetCollisionObject()!=nullptr}});
    if (!observer::Finite(out)) return {{"form",Hex(request.ref)},{"name",request.name},{"status","unavailable"},{"reason","Non-finite node transform"}};
    return out;
}
json Snapshot(const observer::Request& request, std::uint64_t gen) {
    const auto started = Now();
    json out{{"schemaVersion",1},{"observerVersion","0.3.0-se104"},{"ok",true},
        {"sessionId",session},{"loadGeneration",gen},{"sampleId",++sample},
        {"producerFrame",nullptr},{"producerMonotonicNs",started},
        {"phase","skse_main_thread_task"},{"units","skyrim_engine_units"},{"space","world"},
        {"coherence",{{"mode","single_main_thread_task"},{"physics","not_sampled"},{"render","not_synchronized"}}},
        {"refs",json::array()},{"nodes",json::array()},
        {"physics",{{"status","unavailable"},{"reason","No safe phase collector"}}},
        {"render",{{"status","unavailable"},{"reason","No renderer provider"}}}};
    out["vrPicking"]={{"status","unavailable"},{"reason","Flat game target"}};
    bool complete = true;
    for (auto id : request.refs) { auto value=ReadRef(id,gen,request.actorState); complete &= value["status"]=="available"; out["refs"].push_back(std::move(value)); }
    for (const auto& n : request.nodes) { auto value=ReadNode(n,gen); complete &= value["status"]=="available"; out["nodes"].push_back(std::move(value)); }
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
        auto request=observer::ParseSE(args);
        const auto action=args.value("action",std::string("snapshot"));
        if (action=="capabilities") result=Capabilities();
        else {
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
                    const bool initial = job->gen == 0;
                    if (!job->gate.Begin(Now(),generation.load(),initial ? false : loading.load())) {
                        job->promise.set_value(Error("Queued observation expired or load generation changed","abandoned_before_start")); return;
                    }
                    try {
                        if (initial) {
                            if (!InitialWorldReadable()) {
                                job->promise.set_value(Error("Initial world is not readable","read_failed"));
                                job->gate.Finish(); return;
                            }
                            job->gen = ++generation;
                            loading.store(false);
                            if (api) {
                                const json event{{"schemaVersion",1},{"sessionId",session},
                                    {"loadGeneration",job->gen},{"loading",false},{"event","initialWorldObserved"},
                                    {"basis","loaded_player3d_attached_cell_no_startup_menu"},{"producerMonotonicNs",Now()}};
                                api->EmitEvent("world_observer.lifecycle",event.dump().c_str());
                            }
                        }
                        auto value=Snapshot(job->request,job->gen);
                        if (generation.load()!=job->gen || loading.load()) value=Error("World changed during observation","read_interrupted");
                        else if (Now()>=job->gate.Deadline()) value=Error("Read completed after deadline","read_finished_after_deadline");
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
    const auto output=observer::SerializeSE(std::move(result));
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
    const auto& schema=inputSchema;
    const json descriptor{{"description","Read selected world reference/node state in one bounded main-thread task; no game mutation"},
        {"readOnly",true},{"inputSchema",schema},
        {"capabilities",Capabilities()}};
    api->RegisterToolExtension("inspect","world_observer",descriptor.dump().c_str(),Handle,nullptr);
    spdlog::info("Registered inspect world_observer, host build {}",api->GetBuildNumber());
}
void OnMessage(SKSE::MessagingInterface::Message* m) {
    if (!m) return;
    if (m->type==SKSE::MessagingInterface::kPostPostLoad) Connect();
    if (m->type==SKSE::MessagingInterface::kPreLoadGame || m->type==SKSE::MessagingInterface::kNewGame) { ++generation; loading.store(true); }
    // SKSE's load result is encoded in the pointer value, not a pointed-to bool.
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
extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = [] {
    SKSE::PluginVersionData value{};
    value.PluginVersion({0,3,0,0});
    value.PluginName("SkyrimWorldObserver");
    value.AuthorName("Vhodnoylogin");
    value.UsesAddressLibrary();
    value.UsesUpdatedStructs();
    value.MinimumRequiredXSEVersion({2,3,1,0});
    value.CompatibleVersions({SKSE::RUNTIME_SSE_1_7_104});
    return value;
}();
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface* skse,SKSE::PluginInfo* info) {
    if (!skse || !info) return false;
    info->infoVersion=SKSE::PluginInfo::kVersion; info->name="SkyrimWorldObserver"; info->version=0x00030000;
    return !skse->IsEditor() && skse->RuntimeVersion()==SKSE::RUNTIME_SSE_1_7_104;
}
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    if (!skse || skse->IsEditor() || skse->RuntimeVersion()!=SKSE::RUNTIME_SSE_1_7_104) return false;
    SKSE::Init(skse);
    if (auto dir=SKSE::log::log_directory()) {
        *dir/="SkyrimWorldObserver.log";
        auto log=std::make_shared<spdlog::logger>("world_observer",std::make_shared<spdlog::sinks::basic_file_sink_mt>(dir->string(),true));
        spdlog::set_default_logger(log); spdlog::flush_on(spdlog::level::info);
    }
    auto messaging=SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) return false;
    spdlog::info("World observer 0.3.0-se104 loaded, session {}",session);
    return true;
}
