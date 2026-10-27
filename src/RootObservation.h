#pragma once
#include <cstdint>
#include <map>

namespace observer {
// Main-thread-only sampled identity. No object ownership and no raw address in output.
// Unobserved unload/reload with address reuse cannot be detected by this mechanism.
class RootObservation {
public:
    std::uint64_t Observe(std::uint64_t generation, std::uint32_t form,
        std::uint32_t handle, bool firstPerson, std::uintptr_t address) {
        if (generation != generation_) { roots_.clear(); generation_ = generation; }
        const auto key = (std::uint64_t(form) << 1) | (firstPerson ? 1 : 0);
        if (!roots_.contains(key) && roots_.size() >= 128) roots_.clear();
        auto& value = roots_[key];
        if (!value.seen || value.handle != handle || value.address != address) {
            value = {handle, address, address ? ++sequence_ : 0, true};
        }
        return value.token;
    }
private:
    struct Value { std::uint32_t handle{}; std::uintptr_t address{}; std::uint64_t token{}; bool seen{}; };
    std::uint64_t generation_ = 0, sequence_ = 0;
    std::map<std::uint64_t, Value> roots_;
};
}
