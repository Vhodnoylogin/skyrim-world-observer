#include "SEContract.h"
#include <iostream>
#include <stdexcept>
using observer::json;
void Require(bool value) { if (!value) throw std::runtime_error("SE observation contract failed"); }
template<class F> void Reject(F fn) { bool bad=false; try { fn(); } catch (const std::exception&) { bad=true; } Require(bad); }
int main() {
    Require(observer::ParseSE(json{{"kind","world_observer"}}).refs == std::vector<std::uint32_t>{20});
    Require(observer::ParseSE(json{{"action","capabilities"}}).refs.size() == 1);
    for (auto key : {"physics","vrPicking","actorState","render","command"})
        Reject([&] { observer::ParseSE(json{{key,false}}); });
    Reject([] { observer::ParseSE(json{{"kind","world_wrong"}}); });
    Reject([] { observer::ParseSE(json{{"action","capture"}}); });
    Reject([] { observer::ParseSE(json{{"timeoutMs",99}}); });
    Reject([] { observer::ParseSE(json{{"timeoutMs",true}}); });
    const auto extended=observer::ParseSE(json{{"actors",true},
        {"skee",{{"refs",json::array({20})},{"morphs",json::array({"ZeroMorph"})}}},
        {"obody",{{"afterSequence",0},{"maximumEvents",1},{"refs",json::array({"0x14"})}}},{"cbpc",true}});
    Require(extended.actors && extended.cbpc && extended.skeeRefs[0]==20 && extended.morphs[0]=="ZeroMorph");
    Require(extended.obody["refs"][0]==20);
    Reject([] { observer::ParseSE(json{{"actors",1}}); });
    Reject([] { observer::ParseSE(json{{"skee",{{"refs",json::array({20})},{"morphs",json::array()}}}}); });
    Reject([] { observer::ParseSE(json{{"skee",{{"refs",json::array({20})},{"morphs",json::array({std::string("a\0b",3)})}}}}); });
    Reject([] { observer::ParseSE(json{{"obody",{{"afterSequence",-1}}}}); });
    Reject([] { observer::ParseSE(json{{"obody",{{"afterSequence",true}}}}); });
    Reject([] { observer::ParseSE(json{{"obody",{{"maximumEvents",257}}}}); });
    Reject([] { observer::ParseSE(json{{"obody",{{"claim",true}}}}); });
    Reject([] { observer::ParseSE(json{{"cbpc",json::object()}}); });
    Reject([] { observer::ParseSE(json{{"refs",json::array({0})}}); });
    Reject([] { observer::ParseSE(json{{"nodes",json::array({{{"ref",20},{"name","Hand"},{"offset",0}}})}}); });
    const auto clean=json::parse(observer::SerializeSE(json{{"ok",true},{"sessionId","sample"}}));
    Require(clean["ok"] == true && !clean.contains("textEncodingLossy"));
    const auto lossy=json::parse(observer::SerializeSE(json{{"ok",true},{"label",std::string(1,char(0xff))},{"runtimeHandle",123}}));
    Require(lossy["runtimeHandle"] == 123 && lossy["quality"] == "partial" && lossy["textEncodingLossy"] == true);
    std::cout << "SE subset request and native-text safety checks passed\n";
}
