#pragma once
#include "Contract.h"

namespace observer {
struct SERequest : Request {
    bool actors = false;
    std::vector<std::uint32_t> skeeRefs;
    std::vector<std::string> morphs;
    json obody;
    bool cbpc = false;
};
// The SE subset must reject domains whose layouts/providers were not ported.
inline SERequest ParseSE(const json& args) {
    if (!args.is_object()) throw std::invalid_argument("request must be an object");
    for (const auto& [key, unused] : args.items()) {
        (void)unused;
        if (key != "kind" && key != "action" && key != "refs" && key != "nodes" && key != "timeoutMs" &&
            key != "actors" && key != "skee" && key != "obody" && key != "cbpc")
            throw std::invalid_argument("unsupported SE observation field: " + key);
    }
    if (args.contains("kind") && args.at("kind") != "world_observer")
        throw std::invalid_argument("wrong observer kind");
    const auto action = args.value("action", std::string("snapshot"));
    if (action != "snapshot" && action != "capabilities")
        throw std::invalid_argument("unsupported SE observation action");
    auto base = args;
    for (auto key : {"actors", "skee", "obody", "cbpc"}) base.erase(key);
    SERequest r;
    static_cast<Request&>(r) = Parse(base);
    if (args.contains("actors")) {
        if (!args["actors"].is_boolean()) throw std::invalid_argument("actors must be boolean");
        r.actors = args["actors"].get<bool>();
    }
    if (args.contains("skee")) {
        const auto& s = args["skee"];
        if (!s.is_object() || s.size() != 2 || !s.contains("refs") || !s.contains("morphs") ||
            !s["refs"].is_array() || s["refs"].empty() || s["refs"].size() > 16 ||
            !s["morphs"].is_array() || s["morphs"].empty() || s["morphs"].size() > 32 ||
            s["refs"].size() * s["morphs"].size() > 64)
            throw std::invalid_argument("skee requires refs1..16 and morphs1..32, at most64 actor/morph pairs");
        for (const auto& id : s["refs"]) r.skeeRefs.push_back(Form(id));
        for (const auto& value : s["morphs"]) {
            if (!value.is_string()) throw std::invalid_argument("morph must be a name");
            auto name = value.get<std::string>();
            if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos)
                throw std::invalid_argument("morph name must be1..128 bytes without NUL");
            r.morphs.push_back(std::move(name));
        }
    }
    if (args.contains("obody")) {
        const auto& o = args["obody"];
        if (!o.is_object()) throw std::invalid_argument("obody must be an object");
        for (const auto& [key, unused] : o.items()) {
            (void)unused;
            if (key != "refs" && key != "afterSequence" && key != "maximumEvents")
                throw std::invalid_argument("unsupported obody field");
        }
        r.obody = o;
        if (o.contains("refs")) {
            if (!o["refs"].is_array() || o["refs"].empty() || o["refs"].size() > 16)
                throw std::invalid_argument("obody refs must contain1..16 FormIDs");
            r.obody["refs"] = json::array();
            for (const auto& id : o["refs"]) r.obody["refs"].push_back(Form(id));
        }
        if (o.contains("afterSequence") && (!o["afterSequence"].is_number_integer() || o["afterSequence"] < 0))
            throw std::invalid_argument("obody afterSequence must be nonnegative uint64");
        if (o.contains("maximumEvents") && (!o["maximumEvents"].is_number_integer() ||
                o["maximumEvents"] < 1 || o["maximumEvents"] > 256))
            throw std::invalid_argument("obody maximumEvents must be1..256");
    }
    if (args.contains("cbpc")) {
        if (!args["cbpc"].is_boolean()) throw std::invalid_argument("cbpc must be boolean");
        r.cbpc = args["cbpc"].get<bool>();
    }
    return r;
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
