#include "OBodyObservation.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
// The official API intentionally leaves game types undefined. Pointer-only ABI
// types match its global declarations without importing or changing its header.
class Actor;
class TESForm;
#include <OBodyAPI.h>

namespace observer::obody {
namespace {
Ring ring;
// Serialize read-only getter/view-copy use against readiness invalidation.
// This is separate from Ring: no ring lock is held over a provider call.
std::mutex providerReadGate;
OBody::API::IPluginInterface* provider = nullptr;
std::atomic<bool> initialized{false}, actorRegistered{false};
bool modRegistered = false;

class ActorEvents final : public OBody::API::IActorChangeEventListener {
public:
    using Generated = struct OBody::API::IActorChangeEventListener::OnActorGenerated;
    using Changed = struct OBody::API::IActorChangeEventListener::OnActorPresetChangedWithoutGeneration;
    Generated::Response OnActorGenerated(::Actor* actor, Generated::Flags flags, Generated::Payload& payload) override {
        Record("OnActorGenerated", actor, static_cast<std::uint64_t>(flags), payload.presetName,
            payload.responsiblePluginInterface);
        return Generated::Response::None;
    }
    Changed::Response OnActorPresetChangedWithoutGeneration(::Actor* actor, Changed::Flags flags, Changed::Payload& payload) override {
        Record("OnActorPresetChangedWithoutGeneration", actor, static_cast<std::uint64_t>(flags),
            payload.presetName, payload.responsiblePluginInterface);
        return Changed::Response::None;
    }
private:
    static void Record(std::string_view kind, ::Actor* actor, std::uint64_t flags, std::string_view preset,
                       OBody::API::IPluginInterfaceVersionIndependent* responsible) noexcept {
        try {
            const auto typedActor = reinterpret_cast<RE::Actor*>(actor);
            ring.NativeEvent(kind, typedActor ? typedActor->GetFormID() : 0, flags, preset,
                responsible != nullptr, responsible ? responsible->owner : nullptr);
        } catch (...) { ring.CallbackFailed(); }
    }
} actorEvents;

class Readiness final : public OBody::API::IOBodyReadinessEventListener {
public:
    void OBodyIsBecomingReady() override { Record("becoming_ready"); }
    void OBodyIsReady() override {
        try {
            // No ring lock crosses the provider call, and this is a readiness
            // callback, never an actor-change callback. Keep one process-lifetime
            // listener: OBody retains actor listeners across ready epochs.
            if (provider && !actorRegistered.load()) {
                const bool registered = provider->RegisterEventListener(actorEvents);
                actorRegistered.store(registered);
                ring.ListenerRegistered(registered);
            }
            std::lock_guard gate(providerReadGate);
            ring.Readiness("ready");
        } catch (...) { ring.CallbackFailed(); }
    }
    void OBodyIsBecomingUnready() override { Record("becoming_unready"); }
    void OBodyIsNoLongerReady() override { Record("unready"); }
private:
    static void Record(std::string_view state) noexcept {
        try {
            std::lock_guard gate(providerReadGate);
            ring.Readiness(state);
        } catch (...) { ring.CallbackFailed(); }
    }
} readiness;

class ModEvents final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
        if (!event) return RE::BSEventNotifyControl::kContinue;
        try {
            const std::string_view name(event->eventName.c_str(), event->eventName.length());
            if (SelectedModEvent(name)) {
                // BSFixedString lengths are obtained without creating an
                // intermediate owning string. Retained text is bounded by Ring.
                ring.ModEvent(name, std::string_view(event->strArg.c_str(), event->strArg.length()),
                    event->numArg, event->sender ? event->sender->GetFormID() : 0);
            }
        } catch (...) { ring.CallbackFailed(); }
        return RE::BSEventNotifyControl::kContinue;
    }
} modEvents;

json Assignments(const Query& query, const std::function<bool()>& stillCurrent) {
    json values = json::array();
    bool interrupted = false;
    for (const auto id : query.refs) {
        json value{{"actorForm", HexForm(id)}, {"status", "unavailable"}};
        if (stillCurrent && !stillCurrent()) interrupted = true;
        if (interrupted) {
            value["reason"] = "World generation or observation deadline no longer current";
            values.push_back(std::move(value));
            continue;
        }
        // The pinned provider sends becoming_unready while API use is still
        // safe, before rearranging its storage. That callback must acquire this
        // gate and disable further reads before returning. The getter itself
        // does not dispatch readiness or actor-change callbacks.
        std::lock_guard gate(providerReadGate);
        if (stillCurrent && !stillCurrent()) interrupted = true;
        const auto before = ring.State();
        if (interrupted) value["reason"] = "World generation or observation deadline no longer current";
        else if (!before.available || !provider) value["reason"] = "OBody native interface unavailable";
        else if (!before.ready) value["reason"] = "OBody native readiness callback does not permit reads";
        else {
            const auto form = RE::TESForm::LookupByID(id);
            const auto actor = form ? form->As<RE::Actor>() : nullptr;
            if (!actor || actor->IsDeleted()) value["reason"] = "Actor missing or deleted";
            else {
                OBody::API::PresetAssignmentInformation info{};
                info.flags = OBody::API::PresetAssignmentInformation::Flags::None;
                provider->GetPresetAssignedToActor(reinterpret_cast<::Actor*>(actor), info);
                // The view becomes invalid at the next unready boundary; copy
                // before making any further provider or engine calls.
                const auto name = CopyText(info.presetName);
                if (stillCurrent && !stillCurrent()) interrupted = true;
                const auto after = ring.State();
                if (interrupted)
                    value["reason"] = "World generation or observation deadline changed during assignment read";
                else if (!after.ready || before.epoch != after.epoch)
                    value["reason"] = "Provider readiness epoch changed during assignment read";
                else {
                    value.update({{"status", "available"}, {"providerEpoch", before.epoch},
                        {"phase", "skse_main_thread_provider_getter"}, {"flags", static_cast<std::uint64_t>(info.flags)},
                        {"assigned", !info.presetName.empty()}, {"presetName", TextJSON(name)},
                        {"coherence", "sampled_provider_assignment_not_applied_or_rendered_morphs"}});
                }
            }
        }
        values.push_back(std::move(value));
    }
    return values;
}
}  // namespace

void Initialize() {
    if (initialized.exchange(true)) return;
    if (auto* source = SKSE::GetModCallbackEventSource()) {
        source->AddEventSink(&modEvents);
        modRegistered = true;
    }
    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        ring.Connection(false, modRegistered, "SKSE messaging interface unavailable");
        return;
    }
    OBody::API::SKSEMessages::RequestPluginInterface request{};
    request.version = OBody::API::PluginAPIVersion::v1;
    request.pluginInterface = &provider;
    request.readinessEventListener = &readiness;
    // Official request may synchronously invoke readiness callbacks. The pointer
    // must be written directly into the process-lifetime location above.
    messaging->Dispatch(decltype(request)::type, &request, sizeof(request), "OBody");
    ring.Connection(provider != nullptr, modRegistered, provider ?
        "Official OBody native interface v1 acquired; loaded provider source equivalence unverified" :
        "OBody absent or official native interface v1 request not satisfied");
}
void WorldChanged(std::uint64_t generation, bool loading, std::string_view reason) {
    ring.WorldChanged(generation, loading, reason);
}
json Snapshot(const json& request, const std::function<bool()>& stillCurrent) {
    const auto query = ParseQuery(request);
    auto out = ring.Snapshot(query);
    out["assignments"] = Assignments(query, stillCurrent);
    for (const auto& assignment : out["assignments"])
        if (assignment.at("status") != "available" ||
            (assignment.contains("presetName") && assignment["presetName"].value("truncated", false)))
            out["quality"] = "partial";
    out["assignmentReadMode"] = "explicit_refs_only_sampled_no_atomicity";
    return out;
}
json Capabilities() {
    json selected = json::array();
    for (auto name : ModEventNames) selected.push_back(name);
    return {{"readOnly", true}, {"eventCapacity", EventCapacity}, {"maximumEvents", EventCapacity},
        {"textBytes", TextLimit}, {"ownerBytes", OwnerLimit}, {"selectedModCallbacks", std::move(selected)},
        {"nativeAPI", "OBody plugin API v1"}, {"currentAssignment", "readiness_guarded_main_thread_getter"},
        {"stockInternalGeneration", "native_OnActorGenerated_null_responsiblePluginInterface_only"},
        {"hostRequestClaimCompletion", false}, {"renderCompletion", false},
        {"providerAvailability", ring.State().available}};
}
}  // namespace observer::obody
