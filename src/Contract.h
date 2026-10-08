#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace observer {
using json = nlohmann::json;
inline bool InitialWorldReadable(std::uint64_t generation, bool loading,
                                 bool player3D, bool attachedCell, bool playroom,
                                 bool paused, bool startupMenu) {
    return generation == 0 && loading && player3D && attachedCell && !playroom && !paused && !startupMenu;
}
inline std::uint32_t Form(const json& value) {
    if (value.is_number_integer()) {
        if (value.is_number_unsigned()) {
            const auto n = value.get<std::uint64_t>();
            if (n && n <= UINT32_MAX) return static_cast<std::uint32_t>(n);
        } else {
            const auto n = value.get<std::int64_t>();
            if (n > 0 && n <= UINT32_MAX) return static_cast<std::uint32_t>(n);
        }
    } else if (value.is_string()) {
        const auto s = value.get<std::string>();
        std::uint32_t n = 0;
        if (s.size() >= 3 && s.size() <= 10 && s.starts_with("0x")) {
            const auto parsed = std::from_chars(s.data()+2, s.data()+s.size(), n, 16);
            if (parsed.ec == std::errc{} && parsed.ptr == s.data()+s.size() && n) return n;
        }
    }
    throw std::invalid_argument("ref must be nonzero uint32 or 0x hexadecimal FormID");
}
struct NodeRequest { std::uint32_t ref; std::string name; bool firstPerson; };
struct Request {
    std::vector<std::uint32_t> refs;
    std::vector<NodeRequest> nodes;
    int timeoutMs = 1500;
    std::vector<std::uint32_t> physicsRefs;
    std::uint64_t afterSequence = 0;
    bool actorState = false;
    std::string captureAction, captureId;
    std::int64_t captureWindowNs = 0;
    std::size_t maximumSamples = 256;
};
inline Request Parse(const json& args) {
    if (!args.is_object()) throw std::invalid_argument("request must be an object");
    Request r;
    for (const auto& [key, unused] : args.items()) {
        (void)unused;
        if (key != "kind" && key != "action" && key != "refs" && key != "nodes" && key != "timeoutMs" && key != "physics" && key != "actorState")
            throw std::invalid_argument("unsupported request field: " + key);
    }
    if (args.contains("physics")) {
        const auto& p=args["physics"];
        if (!p.is_object() || !p.contains("refs") || !p["refs"].is_array() || p["refs"].empty() || p["refs"].size()>16)
            throw std::invalid_argument("physics requires refs array of 1..16 FormIDs");
        for (const auto& [key, unused]:p.items()) {
            (void)unused;
            if(key!="refs" && key!="afterSequence" && key!="captureAction" && key!="captureId" && key!="captureWindowMs" && key!="maximumSamples") throw std::invalid_argument("unsupported physics field");
        }
        for(const auto& ref:p["refs"]) r.physicsRefs.push_back(Form(ref));
        if(p.contains("afterSequence")) {
            const auto& n=p["afterSequence"];
            if(!n.is_number_integer() || (!n.is_number_unsigned() && n.get<std::int64_t>()<0))
                throw std::invalid_argument("afterSequence must be nonnegative uint64");
            r.afterSequence=n.get<std::uint64_t>();
        }
        if(p.contains("maximumSamples")) {
            if(!p["maximumSamples"].is_number_integer() || p["maximumSamples"]<1 || p["maximumSamples"]>256) throw std::invalid_argument("maximumSamples outside1..256");
            r.maximumSamples=p["maximumSamples"].get<std::size_t>();
        }
        if(p.contains("captureAction")) {
            r.captureAction=p.at("captureAction").get<std::string>();r.captureId=p.at("captureId").get<std::string>();
            if((r.captureAction!="start" && r.captureAction!="read") || r.captureId.size()!=32 ||
               r.captureId.find_first_not_of("0123456789abcdef")!=std::string::npos) throw std::invalid_argument("Exact capture action/id required");
            if(r.captureAction=="start") {
                const auto& ms=p.at("captureWindowMs");
                if(!ms.is_number_integer() || ms<100 || ms>60000) throw std::invalid_argument("Capture window outside100..60000ms");
                r.captureWindowNs=ms.get<std::int64_t>()*1000000;
            } else if(p.contains("captureWindowMs")) throw std::invalid_argument("Read cannot extend capture window");
        } else if(p.contains("captureId") || p.contains("captureWindowMs")) throw std::invalid_argument("Capture action required");
    }
    if(args.contains("actorState")) {
        if(!args["actorState"].is_boolean()) throw std::invalid_argument("actorState must be boolean");
        r.actorState=args["actorState"].get<bool>();
    }
    if (args.contains("timeoutMs")) {
        if (!args["timeoutMs"].is_number_integer()) throw std::invalid_argument("timeoutMs must be integer");
        const auto& timeout=args["timeoutMs"];
        if (timeout.is_number_unsigned()) {
            const auto n=timeout.get<std::uint64_t>();
            if (n<100 || n>3000) throw std::invalid_argument("timeoutMs outside 100..3000");
            r.timeoutMs=static_cast<int>(n);
        } else {
            const auto n=timeout.get<std::int64_t>();
            if (n<100 || n>3000) throw std::invalid_argument("timeoutMs outside 100..3000");
            r.timeoutMs=static_cast<int>(n);
        }
    }
    if (args.contains("refs")) {
        if (!args["refs"].is_array() || args["refs"].size() > 16) throw std::invalid_argument("refs must be array <=16");
        for (const auto& v : args["refs"]) r.refs.push_back(Form(v));
    }
    if (args.contains("nodes")) {
        if (!args["nodes"].is_array() || args["nodes"].size() > 64) throw std::invalid_argument("nodes must be array <=64");
        for (const auto& v : args["nodes"]) {
            if (!v.is_object() || !v.contains("ref") || !v.contains("name") || !v["name"].is_string())
                throw std::invalid_argument("node requires ref and name");
            for (const auto& [k, unused] : v.items()) {
                (void)unused;
                if (k != "ref" && k != "name" && k != "firstPerson") throw std::invalid_argument("unsupported node field");
            }
            const auto name = v["name"].get<std::string>();
            if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos)
                throw std::invalid_argument("node name must be 1..128 bytes without NUL");
            if (v.contains("firstPerson") && !v["firstPerson"].is_boolean()) throw std::invalid_argument("firstPerson must be boolean");
            r.nodes.push_back({Form(v["ref"]), name, v.value("firstPerson", false)});
        }
    }
    if (r.refs.empty() && r.nodes.empty()) r.refs.push_back(0x14);
    return r;
}
enum class State { queued, running, finished, abandoned };
class Gate {
public:
    Gate(std::int64_t deadline, std::uint64_t generation) : deadline_(deadline), generation_(generation) {}
    bool Begin(std::int64_t now, std::uint64_t generation, bool loading) {
        if (now >= deadline_ || generation != generation_ || loading) { Cancel(); return false; }
        auto expected = State::queued;
        return state_.compare_exchange_strong(expected, State::running);
    }
    bool Cancel() { auto expected = State::queued; return state_.compare_exchange_strong(expected, State::abandoned); }
    void Finish() { state_.store(State::finished); }
    State Get() const { return state_.load(); }
    std::int64_t Deadline() const { return deadline_; }
private:
    std::int64_t deadline_;
    std::uint64_t generation_;
    std::atomic<State> state_{State::queued};
};
inline bool Finite(const json& value) {
    if (value.is_number_float()) return std::isfinite(value.get<double>());
    if (value.is_array() || value.is_object()) for (const auto& v : value) if (!Finite(v)) return false;
    return true;
}
}
