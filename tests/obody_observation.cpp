#include "OBodyObservation.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace observer::obody;
using observer::json;
namespace {
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void Reject(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    Require(rejected, "Invalid OBody query accepted");
}
void OwnershipAndProvenance() {
    Ring ring;
    std::string preset = "borrowed preset", owner = "_Papyrus";
    ring.NativeEvent("OnActorGenerated", 20, 7, preset, true, owner.c_str());
    preset.assign("changed after callback"); owner.assign("external");
    ring.NativeEvent("OnActorGenerated", 21, 0, "stock", false, nullptr);
    ring.NativeEvent("OnActorGenerated", 22, 0, "external", true, nullptr);
    ring.NativeEvent("OnActorPresetChangedWithoutGeneration", 20, 1, {}, false, nullptr);
    const auto snapshot = ring.Snapshot({});
    const auto& events = snapshot.at("events");
    Require(events[0]["presetName"]["value"] == "borrowed preset", "Borrowed preset was retained");
    Require(events[0]["responsibleOwner"]["value"] == "_Papyrus", "Borrowed owner was retained");
    Require(events[0]["responsibility"] == "obody_papyrus" && !events[0]["stockInternalGeneration"].get<bool>(), "Papyrus mistaken for stock");
    Require(events[1]["responsibility"] == "obody_internal_stock" && events[1]["stockInternalGeneration"].get<bool>(), "Stock not independently identified");
    Require(events[2]["responsibility"] == "external_plugin" && !events[2]["stockInternalGeneration"].get<bool>(), "Missing external owner mistaken for stock");
    Require(events[3]["presetName"]["value"].is_null() && events[3]["flags"] == 1, "Unassignment receipt lost");
    Require(events[1]["renderCompletion"] == "unavailable", "Callback invented rendered completion");
    Require(ClassifyResponsibility(true, "_Papyrus_other") == Responsibility::externalPlugin, "Owner prefix falsely classified");
    Require(snapshot == ring.Snapshot({}), "Read consumed or changed receipts");
}
void BoundsAndLoss() {
    Ring ring;
    const std::string giant(1000000, 'p'), owner(1000, 'o');
    ring.NativeEvent("OnActorGenerated", 20, 0, giant, true, owner.c_str());
    const auto first = ring.Snapshot({})["events"][0];
    Require(first["presetName"]["value"].get<std::string>().size() == TextLimit, "Preset allocation not bounded");
    Require(first["presetName"]["sourceBytes"] == giant.size() && first["presetName"]["truncated"].get<bool>(), "Preset truncation not disclosed");
    Require(first["responsibleOwner"]["value"].get<std::string>().size() == OwnerLimit && first["responsibleOwner"]["sourceBytesLowerBound"].get<bool>(), "Owner scan unbounded or size guessed");
    for (std::size_t i = 0; i < EventCapacity + 17; ++i)
        ring.NativeEvent("OnActorGenerated", 20, i, "bounded", false, nullptr);
    ring.CallbackFailed();
    auto snapshot = ring.Snapshot({});
    Require(snapshot["events"].size() == EventCapacity, "Ring capacity exceeded");
    Require(snapshot["retention"]["droppedTotal"] == 18 && snapshot["retention"]["missingBeforeRetained"] == 18, "Ring loss hidden");
    Require(snapshot["retention"]["callbackFailuresTotal"] == 1 && snapshot["retention"]["truncatedTextFieldsTotal"] == 2, "Callback or truncation loss hidden");
    Query query; query.afterSequence = 5; query.maximumEvents = 7;
    snapshot = ring.Snapshot(query);
    Require(snapshot["events"].size() == 7 && snapshot["retention"]["moreAvailable"].get<bool>(), "Response bound or paging lost");
    Require(snapshot["retention"]["missingBeforeRetained"] == 13 && snapshot["retention"]["nextAfterSequence"] == 25, "Cursor gap incorrect");
    query.afterSequence = std::numeric_limits<std::uint64_t>::max();
    snapshot = ring.Snapshot(query);
    Require(snapshot["events"].empty() && snapshot["retention"]["cursorAhead"].get<bool>() && snapshot["retention"]["missingBeforeRetained"] == 0, "Future cursor overflowed");
}
void EpochsAndFilter() {
    Ring ring;
    Require(!ring.State().available && !ring.State().ready, "Unavailable provider fabricated");
    ring.Connection(true, true, "official native API acquired");
    ring.Readiness("becoming_ready");
    ring.ListenerRegistered(true);
    ring.Readiness("ready");
    ring.WorldChanged(7, false, "new_game");
    ring.NativeEvent("OnActorGenerated", 20, 0, "before load", false, nullptr);
    ring.Readiness("becoming_unready");
    ring.Readiness("unready");
    ring.WorldChanged(8, true, "pre_load");
    ring.Readiness("becoming_ready");
    ring.Readiness("ready");
    ring.WorldChanged(8, false, "post_load");
    ring.NativeEvent("OnActorGenerated", 30, 0, "other actor", true, "foreign");
    ring.NativeEvent("OnActorGenerated", 20, 0, "after load", false, nullptr);
    Query query; query.refs = {20};
    const auto snapshot = ring.Snapshot(query);
    std::uint64_t previous = 0, epoch = 0;
    bool oldWorld = false, newWorld = false;
    std::int64_t timestamp = 0;
    for (const auto& event : snapshot["events"]) {
        Require(event["sequence"].get<std::uint64_t>() > previous, "Receipt sequence reset at load");
        Require(event["providerEpoch"].get<std::uint64_t>() >= epoch, "Provider epoch regressed");
        Require(event["producerMonotonicNs"].get<std::int64_t>() >= timestamp, "Receipt clock regressed");
        previous = event["sequence"]; epoch = event["providerEpoch"]; timestamp = event["producerMonotonicNs"];
        Require(event["actorForm"].is_null() || event["actorForm"] == "0x00000014", "Actor filter leaked other actor");
        if (event["event"] == "OnActorGenerated") {
            oldWorld |= event["loadGeneration"] == 7;
            newWorld |= event["loadGeneration"] == 8;
        }
    }
    Require(oldWorld && newWorld && snapshot["provider"]["epoch"] == 3, "World or provider epoch evidence erased");
    Require(snapshot["sourceHostRequestCompletion"]["status"] == "unavailable", "Passive receipt invented consuming claim");
    query.afterSequence = snapshot["retention"]["latestSequence"];
    Require(ring.Snapshot(query)["events"].empty(), "Cursor replayed consumed range");
    // A filtered-out tail advances a fully drained cursor to the global watermark.
    ring.NativeEvent("OnActorGenerated", 30, 0, "tail", false, nullptr);
    const auto tail = ring.Snapshot(query);
    Require(tail["events"].empty() && tail["retention"]["nextAfterSequence"] == tail["retention"]["latestSequence"], "Filtered cursor cannot drain");
}
void PassiveEvents() {
    Ring ring;
    Require(!ring.ModEvent("arbitrary-event", "discard", 0, 20), "Unselected ModCallback accepted");
    Require(ring.ModEvent(ModEventNames[0], "wolf", 123, 20), "Real candidate receipt ignored");
    Require(ring.ModEvent(ModEventNames[1], "wolf", std::numeric_limits<double>::quiet_NaN(), 20), "Real ready receipt ignored");
    const auto snapshot = ring.Snapshot({});
    Require(snapshot["events"].size() == 2 && snapshot["events"][0]["senderForm"] == "0x00000014", "ModCallback sender lost");
    Require(snapshot["events"][0]["provenance"] == "unattributed_mod_event_receipt", "ModCallback authenticated without proof");
    Require(snapshot["events"][1]["numArg"].is_null() && !snapshot["events"][1]["numArgFinite"].get<bool>(), "Nonfinite ModCallback serialized as value");
}
void ConcurrentReceipts() {
    Ring ring;
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) workers.emplace_back([&ring, t] {
        for (int i = 0; i < 200; ++i) ring.NativeEvent("OnActorGenerated", 20 + t, i, "worker", false, nullptr);
    });
    for (auto& worker : workers) worker.join();
    const auto snapshot = ring.Snapshot({});
    Require(snapshot["retention"]["latestSequence"] == 800 && snapshot["retention"]["droppedTotal"] == 800 - EventCapacity, "Concurrent receipts lost without accounting");
    std::uint64_t expected = 800 - EventCapacity + 1;
    for (const auto& event : snapshot["events"]) Require(event["sequence"] == expected++, "Concurrent sequence duplicate or out of order");
}
void Queries() {
    Require(ParseQuery(json::object()).maximumEvents == EventCapacity, "Default query bound changed");
    Require(ParseQuery(json{{"refs", {"0x14", 21}}, {"afterSequence", 4}, {"maximumEvents", 2}}).refs.size() == 2, "Valid query rejected");
    for (const auto& invalid : {json{{"refs", json::array()}}, json{{"refs", {0}}}, json{{"afterSequence", true}},
        json{{"afterSequence", -1}}, json{{"afterSequence", 1.5}}, json{{"maximumEvents", 0}},
        json{{"maximumEvents", 257}}, json{{"maximumEvents", true}}, json{{"claim", 3}}, json{{"refs", "0x14"}}})
        Reject([&] { ParseQuery(invalid); });
}
}  // namespace
int main() {
    OwnershipAndProvenance(); BoundsAndLoss(); EpochsAndFilter(); PassiveEvents(); ConcurrentReceipts(); Queries();
    std::cout << "OBody copied lifetime, provenance, bounded loss, epochs, filters and concurrent receipt checks passed\n";
}
