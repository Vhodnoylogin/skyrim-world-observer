#include "SkeeObservation.h"
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
using namespace observer::skee;

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Morph {
    bool present{};
    float aggregate{};
    std::vector<std::pair<std::string, float>> keys;
};
class MockProvider final : public ReadProvider {
public:
    ProviderIdentity identity{true, 5, 1, ""};
    std::map<std::string, Morph> morphs;
    std::vector<std::string> calls;
    std::function<void()> afterPresence;
    std::function<void()> afterAggregate;
    std::function<void()> afterVisit;
    bool nullKey{};
    ProviderIdentity Identity() const override { return identity; }
    bool HasName(void*, const char* name) override {
        calls.emplace_back(std::string("HasBodyMorphName:") + name);
        if (afterPresence) afterPresence();
        return morphs[name].present;
    }
    float Aggregate(void*, const char* name) override {
        calls.emplace_back(std::string("GetBodyMorphs:") + name);
        if (afterAggregate) afterAggregate();
        return morphs[name].aggregate;
    }
    void VisitKeys(void*, const char* name, KeySink& sink) override {
        calls.emplace_back(std::string("VisitKeys:") + name);
        for (const auto& [key, value] : morphs[name].keys) sink.Visit(key.c_str(), value);
        if (nullKey) sink.Visit(nullptr, 0.0f);
        if (afterVisit) afterVisit();
    }
};
void* Actor() { static int actor; return &actor; }
const json& Row(const json& result, std::size_t index = 0) { return result.at("morphs").at(index); }
std::vector<std::pair<std::string, float>> Keys(std::size_t count) {
    std::vector<std::pair<std::string, float>> out;
    for (std::size_t i = 0; i < count; ++i) out.emplace_back("key" + std::to_string(i), static_cast<float>(i));
    return out;
}
}

int main() {
    MockProvider provider;
    provider.morphs["Human"] = {true, 0.0f, {{"Foreign",0.0f}}};
    provider.morphs["Wolf"] = {true, 125.0f, {{"A",1.0f},{"B",2.0f}}};
    auto result = ObserveProvider(provider, Actor(), {"Human","Absent","Wolf"});
    Require(result["status"] == "available", "ordinary observations available");
    Require(Row(result)["namePresence"]["present"] == true, "present zero retained");
    Require(Row(result)["aggregate"]["value"] == 0.0, "zero aggregate retained");
    Require(Row(result)["keys"]["entries"][0]["value"] == 0.0, "zero keyed value retained");
    Require(Row(result,1)["namePresence"]["present"] == false, "absent name remains distinct");
    Require(Row(result,1)["aggregate"]["value"] == 0.0, "provider absent zero copied separately");
    Require(Row(result,2)["aggregate"]["value"] == 125.0, "aggregate is provider value, never a sum");
    Require(provider.calls == std::vector<std::string>{"HasBodyMorphName:Human","GetBodyMorphs:Human","VisitKeys:Human",
        "HasBodyMorphName:Absent","GetBodyMorphs:Absent","VisitKeys:Absent",
        "HasBodyMorphName:Wolf","GetBodyMorphs:Wolf","VisitKeys:Wolf"}, "only exact read-only API path");
    Require(result["atomic"] == false && result["providerDataRevision"].is_null(), "no fabricated atomicity or revision");
    Require(result["provider"]["providerTraversalBounded"] == false, "uncancellable traversal limit disclosed");
    Require(result["provider"]["loadedBinaryIdentityVerified"] == false, "header pin is not a loaded DLL pin");
    Require(Row(result)["namePresence"]["finishedMonotonicNs"] >= Row(result)["namePresence"]["startedMonotonicNs"], "getter timing");

    for (const auto version : {0U,1U,4U,6U,999U}) {
        MockProvider unsupported;
        unsupported.identity.interfaceVersion = version;
        const auto sample = ObserveProvider(unsupported, Actor(), {"Human"});
        Require(sample["status"] == "unavailable" && unsupported.calls.empty(), "unsupported interface refuses before getters");
    }
    provider.calls.clear();
    provider.identity.available = false;
    provider.identity.reason = "provider disabled";
    result = ObserveProvider(provider, Actor(), {"Human"});
    Require(result["status"] == "unavailable" && provider.calls.empty(), "missing provider refuses");
    Require(result["provider"]["reason"] == "provider disabled", "provider failure reason retained");
    provider.identity.available = true;
    provider.identity.reason.clear();
    result = ObserveProvider(provider, nullptr, {"Human"});
    Require(result["status"] == "unavailable" && provider.calls.empty(), "missing actor refuses");
    result = ObserveProvider(provider, Actor(), {"Human"}, [] { return false; });
    Require(result["status"] == "unavailable" && provider.calls.empty(), "stale before read refuses");

    bool current = true;
    provider.afterVisit = [&] { current = false; };
    result = ObserveProvider(provider, Actor(), {"Human"}, [&] { return current; });
    Require(result["status"] == "unavailable" && result["morphs"].empty(), "changed actor/root/world discards values");
    provider.afterVisit = [&] { ++provider.identity.interfaceEpoch; };
    result = ObserveProvider(provider, Actor(), {"Human"});
    Require(result["status"] == "unavailable" && result["morphs"].empty(), "changed provider discards values");
    provider.afterVisit = {};
    provider.calls.clear();
    current = true;
    provider.afterPresence = [&] { current = false; };
    result = ObserveProvider(provider, Actor(), {"Human"}, [&] { return current; });
    Require(result["status"] == "unavailable" && result["morphs"].empty() && provider.calls.size() == 1,
        "stale after presence starts no further native call");
    provider.afterPresence = {};
    provider.calls.clear();
    provider.afterAggregate = [&] { ++provider.identity.interfaceEpoch; };
    result = ObserveProvider(provider, Actor(), {"Human"});
    Require(result["status"] == "unavailable" && result["morphs"].empty() && provider.calls.size() == 2,
        "provider change after aggregate starts no visitor call");
    provider.afterAggregate = {};

    for (const auto value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}) {
        provider.morphs["Bad"] = {true, value, {{"Foreign",0.0f}}};
        result = ObserveProvider(provider, Actor(), {"Bad"});
        Require(result["status"] == "unavailable" && Row(result)["aggregate"]["value"].is_null(), "nonfinite aggregate unavailable");
        Require(Row(result)["namePresence"]["present"] == true, "nonfinite aggregate does not fabricate absence");
        provider.morphs["Bad"] = {true, 1.0f, {{"Foreign",value}}};
        result = ObserveProvider(provider, Actor(), {"Bad"});
        Require(result["status"] == "unavailable" && Row(result)["keys"]["entries"][0]["value"].is_null(), "nonfinite key unavailable");
        Require(result.dump().find("null") != std::string::npos, "finite JSON serialization");
    }
    provider.morphs["Many"] = {true, 0.0f, Keys(kMaxKeysPerMorph)};
    result = ObserveProvider(provider, Actor(), {"Many"});
    Require(result["status"] == "available" && Row(result)["keys"]["retainedCount"] == kMaxKeysPerMorph, "exact per-morph boundary");
    provider.morphs["Many"].keys = Keys(kMaxKeysPerMorph + 17);
    result = ObserveProvider(provider, Actor(), {"Many"});
    Require(result["status"] == "unavailable" && Row(result)["keys"]["truncated"] == true, "per-morph overflow unavailable");
    Require(Row(result)["keys"]["retainedCount"] == kMaxKeysPerMorph &&
        Row(result)["keys"]["callbackCount"] == kMaxKeysPerMorph + 17, "only output bounded, callbacks accurately counted");
    std::vector<std::string> names;
    for (int i = 0; i < 5; ++i) {
        names.push_back("Morph" + std::to_string(i));
        provider.morphs[names.back()] = {true, 0.0f, Keys(kMaxKeysPerMorph)};
    }
    result = ObserveProvider(provider, Actor(), names);
    Require(result["retainedKeyValueCount"] == kMaxKeyValuesPerActor, "actor-wide output bound");
    Require(Row(result,4)["keys"]["retainedCount"] == 0 && Row(result,4)["keys"]["truncated"] == true,
        "later exhausted collection never becomes empty available");

    provider.morphs["Text"] = {true, 0.0f, {{std::string(kMaxNameBytes,'x'),0.0f}}};
    result = ObserveProvider(provider, Actor(), {"Text"});
    Require(result["status"] == "available", "exact key string limit");
    for (const auto& key : {std::string(),std::string(kMaxNameBytes+1,'x')}) {
        provider.morphs["Text"].keys = {{key,0.0f}};
        result = ObserveProvider(provider, Actor(), {"Text"});
        Require(result["status"] == "unavailable" && Row(result)["keys"]["invalidKey"] == true &&
            Row(result)["keys"]["entries"].empty(), "bad keys not silently aliased/truncated");
    }
    provider.morphs["Text"].keys.clear();
    provider.nullKey = true;
    result = ObserveProvider(provider, Actor(), {"Text"});
    Require(result["status"] == "unavailable" && Row(result)["keys"]["invalidKey"] == true, "null provider key refuses");
    provider.nullKey = false;
    const std::vector<std::vector<std::string>> badLists = {{},{""},{std::string(129,'x')},
        {std::string("Human\0Wolf",10)},std::vector<std::string>(33,"Human")};
    for (const auto& bad : badLists) {
        provider.calls.clear();
        result = ObserveProvider(provider, Actor(), bad);
        Require(result["status"] == "unavailable" && provider.calls.empty(), "invalid input has zero provider calls");
    }
    std::cout << "SKEE zero/presence, aggregate source, provider/stale guards, finite values and bounded output checks passed\n";
}
