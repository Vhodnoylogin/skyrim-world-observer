#pragma once
#include "Contract.h"

namespace observer {
// The SE subset must reject domains whose layouts/providers were not ported.
inline Request ParseSE(const json& args) {
    if (!args.is_object()) throw std::invalid_argument("request must be an object");
    for (const auto& [key, unused] : args.items()) {
        (void)unused;
        if (key != "kind" && key != "action" && key != "refs" && key != "nodes" && key != "timeoutMs")
            throw std::invalid_argument("unsupported SE observation field: " + key);
    }
    if (args.contains("kind") && args.at("kind") != "world_observer")
        throw std::invalid_argument("wrong observer kind");
    const auto action = args.value("action", std::string("snapshot"));
    if (action != "snapshot" && action != "capabilities")
        throw std::invalid_argument("unsupported SE observation action");
    return Parse(args);
}
inline std::string SerializeSE(json result) {
    try { return result.dump(); }
    catch (const json::type_error&) {
        // Native file/node labels can contain invalid engine text. Preserve
        // numeric identity, disclose replacement and never throw across WriteFn.
        result["textEncodingLossy"] = true;
        result["quality"] = "partial";
        return result.dump(-1, ' ', false, json::error_handler_t::replace);
    }
}
}
