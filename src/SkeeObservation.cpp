#include "SkeeObservation.h"
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#if defined(OBSERVER_WITH_SKEE_RUNTIME)
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <IPluginInterface.h> // Exact hash-pinned external official header.
#include <mutex>
#endif

namespace observer::skee {
namespace {
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool Current(const StillCurrent& guard) { return !guard || guard(); }
bool ValidNames(const std::vector<std::string>& names) {
    if (names.empty() || names.size() > kMaxMorphs) return false;
    for (const auto& name : names)
        if (name.empty() || name.size() > kMaxNameBytes || name.find('\0') != std::string::npos) return false;
    return true;
}
json Base(const ProviderIdentity& identity) {
    return {{"status","available"},{"provider",DescribeProvider(identity)},
        {"phase","skse_main_thread_task"},{"coherence","sequential_provider_reads"},
        {"atomic",false},{"providerDataRevision",nullptr},
        {"renderedEffectObserved",false},{"morphs",json::array()}};
}
json Unavailable(json out, const char* reason) {
    out["status"] = "unavailable";
    out["reason"] = reason;
    // Never leave consumable values attached to a stale actor/provider sample.
    out["morphs"] = json::array();
    return out;
}
struct KeyValue { std::string key; float value; };
class BoundedKeys final : public KeySink {
public:
    explicit BoundedKeys(std::size_t& remaining) : remaining_(remaining) {}
    void Visit(const char* key, float value) noexcept override {
        if (callbacks != std::numeric_limits<std::uint64_t>::max()) ++callbacks;
        // The official void visitor cannot cancel provider iteration. After the
        // output limit only a saturating count and flag are touched per callback.
        if (values.size() >= kMaxKeysPerMorph || remaining_ == 0) { truncated = true; return; }
        if (captureFailure) return;
        if (!key) { invalidKey = true; return; }
        std::size_t length = 0;
        while (length <= kMaxNameBytes && key[length] != '\0') ++length;
        if (length == 0 || length > kMaxNameBytes) { invalidKey = true; return; }
        try { values.push_back({std::string(key, length), value}); }
        catch (...) { captureFailure = true; return; }
        --remaining_;
        nonFinite |= !std::isfinite(value);
    }
    std::vector<KeyValue> values;
    std::uint64_t callbacks{};
    bool truncated{}, invalidKey{}, nonFinite{}, captureFailure{};
private:
    std::size_t& remaining_;
};
}

json DescribeProvider(const ProviderIdentity& identity) {
    json out{{"status",identity.available ? "available" : "unavailable"},
        {"messagingTarget","skee"},{"interface","BodyMorph"},
        {"interfaceVersion",identity.interfaceVersion ? json(identity.interfaceVersion) : json(nullptr)},
        {"interfaceEpoch",identity.interfaceEpoch},
        {"interfaceEpochMeaning","observer_negotiated_interface_identity_not_data_revision"},
        {"supportedInterfaceVersions",json::array({kSupportedInterfaceVersion})},
        {"apiHeaderCommit","9ebcb733e17be695f994cd2e9cc383043446bc02"},
        {"apiHeaderSha256","875410dd22230f76a014a6841004e7d346f5fa1c748cfff695f9f495234da277"},
        {"loadedBinaryIdentityVerified",false},{"providerTraversalBounded",false},
        {"bounds",{{"morphsPerActor",kMaxMorphs},{"nameBytes",kMaxNameBytes},
            {"keysPerMorph",kMaxKeysPerMorph},{"keyValuesPerActor",kMaxKeyValuesPerActor}}}};
    if (!identity.available) out["reason"] = identity.reason;
    return out;
}

json ObserveProvider(ReadProvider& provider, void* actor,
    const std::vector<std::string>& morphs, const StillCurrent& stillCurrent) {
    const auto identity = provider.Identity();
    auto out = Base(identity);
    out["startedMonotonicNs"] = Now();
    if (!ValidNames(morphs)) return Unavailable(std::move(out), "Invalid explicit morph-name list");
    if (!actor) return Unavailable(std::move(out), "Actor is unavailable");
    if (!identity.available) return Unavailable(std::move(out), "SKEE BodyMorph provider unavailable");
    if (identity.interfaceVersion != kSupportedInterfaceVersion)
        return Unavailable(std::move(out), "SKEE BodyMorph interface version is unsupported");
    if (!Current(stillCurrent)) return Unavailable(std::move(out), "Actor, root or world changed before provider reads");
    const auto sampleCurrent = [&] { return Current(stillCurrent) && provider.Identity() == identity; };
    std::size_t remaining = kMaxKeyValuesPerActor;
    bool complete = true;
    for (const auto& name : morphs) {
        if (!sampleCurrent())
            return Unavailable(std::move(out), "Actor, root, world or provider changed during provider reads");
        json row{{"name",name},{"status","available"},{"atomic",false},
            {"coherence","sequential_provider_reads"},{"reasons",json::array()}};
        const auto presenceStart = Now();
        const bool present = provider.HasName(actor, name.c_str());
        row["namePresence"] = {{"method","HasBodyMorphName"},{"status","available"},
            {"present",present},{"startedMonotonicNs",presenceStart},{"finishedMonotonicNs",Now()}};
        if (!sampleCurrent())
            return Unavailable(std::move(out), "Actor, root, world or provider changed during provider reads");
        const auto aggregateStart = Now();
        const auto aggregate = provider.Aggregate(actor, name.c_str());
        const bool finite = std::isfinite(aggregate);
        row["aggregate"] = {{"method","GetBodyMorphs"},{"status",finite ? "available" : "unavailable"},
            {"value",finite ? json(aggregate) : json(nullptr)},
            {"startedMonotonicNs",aggregateStart},{"finishedMonotonicNs",Now()}};
        if (!finite) row["reasons"].push_back("Non-finite provider aggregate");
        if (!sampleCurrent())
            return Unavailable(std::move(out), "Actor, root, world or provider changed during provider reads");
        BoundedKeys keys(remaining);
        const auto keysStart = Now();
        // Do not call provider again from Visit: upstream holds its own lock.
        provider.VisitKeys(actor, name.c_str(), keys);
        json entries = json::array();
        for (const auto& value : keys.values) entries.push_back({{"key",value.key},
            {"status",std::isfinite(value.value) ? "available" : "unavailable"},
            {"value",std::isfinite(value.value) ? json(value.value) : json(nullptr)}});
        const bool keysComplete = !keys.truncated && !keys.invalidKey && !keys.nonFinite && !keys.captureFailure;
        row["keys"] = {{"method","VisitKeys"},{"status",keysComplete ? "available" : "unavailable"},
            {"entries",std::move(entries)},{"callbackCount",keys.callbacks},
            {"retainedCount",keys.values.size()},{"truncated",keys.truncated},
            {"invalidKey",keys.invalidKey},{"nonFinite",keys.nonFinite},
            {"captureFailure",keys.captureFailure},
            {"providerTraversalCompleted",true},{"providerTraversalBounded",false},
            {"startedMonotonicNs",keysStart},{"finishedMonotonicNs",Now()}};
        if (keys.truncated) row["reasons"].push_back("Key collection output limit exceeded");
        if (keys.invalidKey) row["reasons"].push_back("Provider returned an empty, null or oversized key");
        if (keys.nonFinite) row["reasons"].push_back("Non-finite provider key value");
        if (keys.captureFailure) row["reasons"].push_back("Key collection allocation failed");
        if (!finite || !keysComplete) { row["status"] = "unavailable"; complete = false; }
        out["morphs"].push_back(std::move(row));
    }
    if (!sampleCurrent())
        return Unavailable(std::move(out), "Actor, root, world or provider changed during provider reads");
    out["finishedMonotonicNs"] = Now();
    out["retainedKeyValueCount"] = kMaxKeyValuesPerActor - remaining;
    if (!complete) {
        out["status"] = "unavailable";
        out["reason"] = "One or more requested morph observations are incomplete";
    }
    return out;
}

#if defined(OBSERVER_WITH_SKEE_RUNTIME)
namespace {
static_assert(sizeof(InterfaceExchangeMessage) == sizeof(void*));
static_assert(IBodyMorphInterface::kCurrentPluginVersion == kSupportedInterfaceVersion);
std::mutex providerMutex;
IBodyMorphInterface* bodyMorphs{}; // Borrowed static upstream object; never delete/Revert.
ProviderIdentity providerIdentity;
class NativeProvider final : public ReadProvider {
public:
    NativeProvider(IBodyMorphInterface* api, ProviderIdentity identity) : api_(api), identity_(std::move(identity)) {}
    ProviderIdentity Identity() const override {
        std::scoped_lock lock(providerMutex);
        auto current = providerIdentity;
        if (bodyMorphs != api_ || current != identity_) {
            current.available = false;
            current.reason = "SKEE interface changed after sample acquisition";
        }
        return current;
    }
    bool HasName(void* actor, const char* name) override {
        return api_->HasBodyMorphName(static_cast<::TESObjectREFR*>(actor), name);
    }
    float Aggregate(void* actor, const char* name) override {
        return api_->GetBodyMorphs(static_cast<::TESObjectREFR*>(actor), name);
    }
    void VisitKeys(void* actor, const char* name, KeySink& sink) override {
        class Visitor final : public IBodyMorphInterface::MorphKeyVisitor {
        public:
            explicit Visitor(KeySink& target) : target_(target) {}
            void Visit(const char* key, float value) override { target_.Visit(key, value); }
        private:
            KeySink& target_;
        } visitor(sink);
        api_->VisitKeys(static_cast<::TESObjectREFR*>(actor), name, visitor);
    }
private:
    IBodyMorphInterface* api_;
    ProviderIdentity identity_;
};
}

void NegotiateProvider() {
    IBodyMorphInterface* candidate{};
    ProviderIdentity next;
    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) next.reason = "SKSE messaging interface unavailable";
    else {
        InterfaceExchangeMessage exchange;
        const bool sent = messaging->Dispatch(static_cast<std::uint32_t>(InterfaceExchangeMessage::kMessage_ExchangeInterface),
            &exchange, sizeof(exchange), "skee");
        if (!sent || !exchange.interfaceMap) next.reason = "SKEE interface exchange unavailable";
        else {
            const auto base = exchange.interfaceMap->QueryInterface("BodyMorph");
            if (!base) next.reason = "SKEE BodyMorph interface missing";
            else {
                next.interfaceVersion = base->GetVersion();
                if (next.interfaceVersion != kSupportedInterfaceVersion)
                    next.reason = "SKEE BodyMorph interface version is unsupported";
                else {
                    candidate = static_cast<IBodyMorphInterface*>(base);
                    next.available = true;
                    next.reason.clear();
                }
            }
        }
    }
    std::scoped_lock lock(providerMutex);
    next.interfaceEpoch = providerIdentity.interfaceEpoch;
    if (candidate != bodyMorphs || next.available != providerIdentity.available ||
        next.interfaceVersion != providerIdentity.interfaceVersion) ++next.interfaceEpoch;
    bodyMorphs = candidate;
    providerIdentity = std::move(next);
}
json ProviderStatus() {
    std::scoped_lock lock(providerMutex);
    return DescribeProvider(providerIdentity);
}
json Observe(RE::Actor* actor, const std::vector<std::string>& morphs, const StillCurrent& stillCurrent) {
    IBodyMorphInterface* api;
    ProviderIdentity identity;
    { std::scoped_lock lock(providerMutex); api = bodyMorphs; identity = providerIdentity; }
    NativeProvider provider(api, identity);
    if (!actor) return ObserveProvider(provider, nullptr, morphs, stillCurrent);
    const auto handle = actor->GetHandle().native_handle();
    const auto form = actor->GetFormID();
    const auto base = actor->GetActorBase();
    const auto race = actor->GetRace();
    // Holding the old root prevents a freed-address reuse from looking unchanged.
    const RE::NiPointer<RE::NiAVObject> root(actor->Get3D(false));
    const auto current = [&] {
        return Current(stillCurrent) && root && !actor->IsDeleted() &&
            actor->GetHandle().native_handle() == handle && actor->GetFormID() == form &&
            actor->GetActorBase() == base && actor->GetRace() == race && actor->Get3D(false) == root.get();
    };
    return ObserveProvider(provider,
        reinterpret_cast<::TESObjectREFR*>(static_cast<RE::TESObjectREFR*>(actor)), morphs, current);
}
#endif
}
