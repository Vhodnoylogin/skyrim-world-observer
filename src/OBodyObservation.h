#pragma once
#include "Contract.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>

namespace observer::obody {
inline constexpr std::size_t EventCapacity = 256;
inline constexpr std::size_t TextLimit = 512;
inline constexpr std::size_t OwnerLimit = 128;
inline constexpr std::array<std::string_view, 4> ModEventNames{
    "CLAWDistribution_OBodySourceCandidate", "CLAWDistribution_OBodySourceReady",
    "CLAWDistribution_OBodySourceAvailable", "Obody_ApplyMorph"};

struct CopiedText {
    std::string value;
    std::uint64_t sourceBytes = 0;
    bool wasNull = false, truncated = false, sourceBytesLowerBound = false;
};
// Never construct an unbounded owning string from provider-owned memory.
inline CopiedText CopyText(std::string_view value, std::size_t limit = TextLimit) {
    CopiedText out;
    out.wasNull = value.data() == nullptr;
    out.sourceBytes = value.size();
    out.truncated = value.size() > limit;
    if (value.data()) out.value.assign(value.data(), (std::min)(value.size(), limit));
    return out;
}
// Owner is a provider-owned C string. Its exact size is unknown beyond the bound.
inline CopiedText CopyOwner(const char* value) {
    if (!value) return CopyText({}, OwnerLimit);
    std::size_t size = 0;
    while (size <= OwnerLimit && value[size]) ++size;
    auto out = CopyText(std::string_view(value, size), OwnerLimit);
    out.sourceBytesLowerBound = out.truncated;
    return out;
}
inline json TextJSON(const CopiedText& text) {
    return {{"value", text.wasNull ? json(nullptr) : json(text.value)},
            {"sourceBytes", text.sourceBytes}, {"sourceBytesLowerBound", text.sourceBytesLowerBound},
            {"truncated", text.truncated}};
}
inline std::string HexForm(std::uint32_t form) {
    std::string out = "0x00000000";
    constexpr char digits[] = "0123456789ABCDEF";
    for (int i = 9; i >= 2; --i) { out[static_cast<std::size_t>(i)] = digits[form & 15]; form >>= 4; }
    return out;
}
enum class Responsibility { internalStock, papyrus, externalPlugin };
inline Responsibility ClassifyResponsibility(bool interfacePresent, std::string_view owner) {
    if (!interfacePresent) return Responsibility::internalStock;
    return owner == "_Papyrus" ? Responsibility::papyrus : Responsibility::externalPlugin;
}
inline const char* ResponsibilityName(Responsibility value) {
    switch (value) {
        case Responsibility::internalStock: return "obody_internal_stock";
        case Responsibility::papyrus: return "obody_papyrus";
        default: return "external_plugin";
    }
}
struct Query {
    std::vector<std::uint32_t> refs;
    std::uint64_t afterSequence = 0;
    std::size_t maximumEvents = EventCapacity;
};
inline Query ParseQuery(const json& args) {
    if (!args.is_object()) throw std::invalid_argument("obody must be an object");
    Query query;
    for (const auto& [key, unused] : args.items()) {
        (void)unused;
        if (key != "refs" && key != "afterSequence" && key != "maximumEvents")
            throw std::invalid_argument("unsupported obody field: " + key);
    }
    if (args.contains("refs")) {
        const auto& refs = args.at("refs");
        if (!refs.is_array() || refs.empty() || refs.size() > 16)
            throw std::invalid_argument("obody refs must contain 1..16 FormIDs");
        for (const auto& ref : refs) query.refs.push_back(Form(ref));
    }
    if (args.contains("afterSequence")) {
        const auto& n = args.at("afterSequence");
        if (!n.is_number_integer() || (!n.is_number_unsigned() && n.get<std::int64_t>() < 0))
            throw std::invalid_argument("obody afterSequence must be nonnegative uint64");
        query.afterSequence = n.get<std::uint64_t>();
    }
    if (args.contains("maximumEvents")) {
        const auto& n = args.at("maximumEvents");
        if (!n.is_number_integer() || n < 1 || n > EventCapacity)
            throw std::invalid_argument("obody maximumEvents outside 1..256");
        query.maximumEvents = n.get<std::size_t>();
    }
    return query;
}
struct Event {
    std::uint64_t sequence = 0, loadGeneration = 0, providerEpoch = 0, flags = 0;
    std::int64_t monotonicNs = 0;
    std::optional<std::uint32_t> actor, sender;
    std::string kind, channel;
    CopiedText text, owner;
    Responsibility responsibility = Responsibility::externalPlugin;
    bool loading = true, providerReady = false, numberPresent = false;
    double number = 0;
};
inline bool SelectedModEvent(std::string_view name) {
    return std::find(ModEventNames.begin(), ModEventNames.end(), name) != ModEventNames.end();
}

// This owns copied values only. Its lock is never held while invoking a provider.
// Kept independent of Skyrim so retention, ownership and epoch semantics are testable.
class Ring {
public:
    void Connection(bool interfaceAvailable, bool modSinkRegistered, std::string_view reason) {
        std::lock_guard lock(mutex_);
        interfaceAvailable_ = interfaceAvailable;
        modSinkRegistered_ = modSinkRegistered;
        reason_ = CopyText(reason).value;
    }
    void ListenerRegistered(bool registered) {
        std::lock_guard lock(mutex_);
        listenerRegistered_ = registered;
    }
    void Readiness(std::string_view state) {
        std::lock_guard lock(mutex_);
        if (state == "becoming_ready" || state == "becoming_unready" ||
            (state == "ready" && readiness_ != "becoming_ready") ||
            (state == "unready" && readiness_ != "becoming_unready")) ++providerEpoch_;
        ready_ = state == "ready";
        readiness_ = state;
        Event event;
        event.kind = state;
        event.channel = "obody_native_readiness";
        PushLocked(std::move(event));
    }
    void WorldChanged(std::uint64_t generation, bool loading, std::string_view reason) {
        std::lock_guard lock(mutex_);
        generation_ = generation;
        loading_ = loading;
        Event event;
        event.kind = "world_boundary";
        event.channel = "observer_lifecycle";
        event.text = CopyText(reason);
        PushLocked(std::move(event));
    }
    void NativeEvent(std::string_view kind, std::uint32_t actor, std::uint64_t flags,
                     std::string_view preset, bool responsibleInterfacePresent,
                     const char* responsibleOwner) {
        Event event;
        event.kind = kind;
        event.channel = "obody_native_actor_change";
        if (actor) event.actor = actor;
        event.flags = flags;
        event.text = CopyText(preset);
        event.owner = CopyOwner(responsibleOwner);
        event.responsibility = ClassifyResponsibility(responsibleInterfacePresent, event.owner.value);
        std::lock_guard lock(mutex_);
        PushLocked(std::move(event));
    }
    bool ModEvent(std::string_view name, std::string_view text, double number, std::uint32_t sender) {
        if (!SelectedModEvent(name)) return false;
        Event event;
        event.kind = name;
        event.channel = "skse_mod_callback";
        event.text = CopyText(text);
        event.numberPresent = true;
        event.number = number;
        if (sender) event.sender = sender;
        std::lock_guard lock(mutex_);
        PushLocked(std::move(event));
        return true;
    }
    void CallbackFailed() noexcept { ++callbackFailures_; }
    struct ProviderState { bool available, ready; std::uint64_t epoch; };
    ProviderState State() const {
        std::lock_guard lock(mutex_);
        return {interfaceAvailable_, ready_, providerEpoch_};
    }
    json Snapshot(const Query& query) const {
        std::lock_guard lock(mutex_);
        json events = json::array();
        std::uint64_t nextCursor = query.afterSequence;
        bool more = false;
        for (const auto& event : events_) {
            if (event.sequence <= query.afterSequence) continue;
            const auto form = event.actor ? event.actor : event.sender;
            if (!query.refs.empty() && form &&
                std::find(query.refs.begin(), query.refs.end(), *form) == query.refs.end()) continue;
            if (events.size() == query.maximumEvents) { more = true; break; }
            json value{{"sequence", event.sequence}, {"loadGeneration", event.loadGeneration},
                {"providerEpoch", event.providerEpoch}, {"producerMonotonicNs", event.monotonicNs},
                {"loadingAtReceipt", event.loading}, {"providerReadyAtReceipt", event.providerReady},
                {"channel", event.channel}, {"event", event.kind},
                {"worldEpochBinding", "observer_last_observed_lifecycle_at_receipt"},
                {"actorForm", event.actor ? json(HexForm(*event.actor)) : json(nullptr)},
                {"senderForm", event.sender ? json(HexForm(*event.sender)) : json(nullptr)},
                {"text", TextJSON(event.text)}};
            if (event.channel == "obody_native_actor_change") {
                value["flags"] = event.flags;
                value["presetName"] = TextJSON(event.text);
                value["responsibility"] = ResponsibilityName(event.responsibility);
                value["responsibleOwner"] = TextJSON(event.owner);
                value["stockInternalGeneration"] = event.kind == "OnActorGenerated" &&
                    event.responsibility == Responsibility::internalStock;
                value["renderCompletion"] = "unavailable";
            }
            if (event.numberPresent) {
                value["numArg"] = std::isfinite(event.number) ? json(event.number) : json(nullptr);
                value["numArgFinite"] = std::isfinite(event.number);
                value["provenance"] = "unattributed_mod_event_receipt";
            }
            events.push_back(std::move(value));
            nextCursor = event.sequence;
        }
        if (!more) nextCursor = (std::max)(nextCursor, lastSequence_);
        const auto oldest = events_.empty() ? lastSequence_ + 1 : events_.front().sequence;
        const auto gap = oldest > query.afterSequence && oldest - query.afterSequence > 1 ?
            oldest - query.afterSequence - 1 : 0;
        return {{"status", interfaceAvailable_ ? "available" : "unavailable"},
            {"quality", interfaceAvailable_ && listenerRegistered_ && modSinkRegistered_ && ready_ &&
                !gap && !overwritten_ && !callbackFailures_.load() && !truncatedFields_ &&
                query.afterSequence <= lastSequence_ && !more ? "complete" : "partial"},
            {"phase", "copied_callback_receipts"}, {"loadGeneration", generation_}, {"loading", loading_},
            {"provider", {{"interfaceAvailable", interfaceAvailable_}, {"ready", ready_},
                {"epoch", providerEpoch_}, {"readiness", readiness_},
                {"actorListenerRegistered", listenerRegistered_}, {"reason", reason_}}},
            {"modCallbacks", {{"sinkRegistered", modSinkRegistered_},
                {"senderAuthentication", "unavailable"}, {"absenceProvesNoEvent", false}}},
            {"retention", {{"capacity", EventCapacity}, {"retained", events_.size()},
                {"oldestSequence", oldest}, {"latestSequence", lastSequence_},
                {"afterSequence", query.afterSequence}, {"nextAfterSequence", nextCursor},
                {"moreAvailable", more}, {"cursorAhead", query.afterSequence > lastSequence_},
                {"droppedTotal", overwritten_}, {"callbackFailuresTotal", callbackFailures_.load()},
                {"missingBeforeRetained", gap}, {"gapScope", "global_ring_before_actor_filter"},
                {"truncatedTextFieldsTotal", truncatedFields_}}},
            {"actorFilterIncludesLifecycle", true}, {"eventsAcrossWorldEpochs", true},
            {"coverage", "since_listener_registration_only"},
            {"coherence", "receipt_order_no_provider_or_render_atomicity"},
            {"sourceHostRequestCompletion", {{"status", "unavailable"},
                {"reason", "Candidate/Ready receipts do not expose host Request/Claim/GenActor call boundaries"}}},
            {"events", std::move(events)}};
    }
private:
    void PushLocked(Event event) {
        event.sequence = ++lastSequence_;
        event.loadGeneration = generation_;
        event.loading = loading_;
        event.providerReady = ready_;
        event.providerEpoch = providerEpoch_;
        event.monotonicNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        truncatedFields_ += event.text.truncated + event.owner.truncated;
        if (events_.size() == EventCapacity) { events_.pop_front(); ++overwritten_; }
        events_.push_back(std::move(event));
    }
    mutable std::mutex mutex_;
    std::deque<Event> events_;
    std::uint64_t lastSequence_ = 0, overwritten_ = 0, providerEpoch_ = 0, generation_ = 0;
    std::uint64_t truncatedFields_ = 0;
    std::atomic<std::uint64_t> callbackFailures_{0};
    bool interfaceAvailable_ = false, ready_ = false, listenerRegistered_ = false;
    bool modSinkRegistered_ = false, loading_ = true;
    std::string readiness_ = "not_observed", reason_ = "OBody API not requested";
};

// Initialize once from SKSE PostPostLoad. Snapshot is called only in the existing
// bounded main-thread observation task; callbacks never schedule subject actions.
void Initialize();
void WorldChanged(std::uint64_t generation, bool loading, std::string_view reason);
json Snapshot(const json& request, const std::function<bool()>& stillCurrent = {});
json Capabilities();
}  // namespace observer::obody
