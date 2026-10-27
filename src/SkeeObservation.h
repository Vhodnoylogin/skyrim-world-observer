#pragma once

#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace RE { class Actor; }

namespace observer::skee {
using json = nlohmann::json;
inline constexpr std::size_t kMaxMorphs = 32;
inline constexpr std::size_t kMaxNameBytes = 128;
inline constexpr std::size_t kMaxKeysPerMorph = 64;
inline constexpr std::size_t kMaxKeyValuesPerActor = 256;
inline constexpr std::uint32_t kSupportedInterfaceVersion = 5;

// Only our small read-only adapter is implemented by offline test providers.
// This is not a replacement declaration of the external SKEE ABI.
struct ProviderIdentity {
    bool available{};
    std::uint32_t interfaceVersion{};
    std::uint64_t interfaceEpoch{};
    std::string reason{"SKEE interface has not been negotiated"};
    bool operator==(const ProviderIdentity&) const = default;
};
class KeySink {
public:
    virtual ~KeySink() = default;
    virtual void Visit(const char* key, float value) = 0;
};
class ReadProvider {
public:
    virtual ~ReadProvider() = default;
    virtual ProviderIdentity Identity() const = 0;
    virtual bool HasName(void* actor, const char* name) = 0;
    virtual float Aggregate(void* actor, const char* name) = 0;
    virtual void VisitKeys(void* actor, const char* name, KeySink& sink) = 0;
};
using StillCurrent = std::function<bool()>;

// Core has no engine dependencies. Actor and guard are borrowed only for this
// call. A false before/after guard or changed interface identity discards values.
json ObserveProvider(ReadProvider& provider, void* actor,
    const std::vector<std::string>& morphs, const StillCurrent& stillCurrent = {});
json DescribeProvider(const ProviderIdentity& identity);

// Runtime adapter is compiled with OBSERVER_WITH_SKEE_RUNTIME=1. Negotiate from
// SKSE PostLoad and retry PostPostLoad (the provider registers during PostLoad).
// Observe must only be called inside the existing bounded SKSE main-thread task.
// It keeps the loaded root alive and checks actor/root continuity around reads;
// the caller's guard should additionally check the enclosing world generation.
void NegotiateProvider();
json ProviderStatus();
json Observe(RE::Actor* actor, const std::vector<std::string>& morphs,
    const StillCurrent& stillCurrent = {});
}
