#include "Contract.h"
#include <iostream>
#include <limits>
using observer::json;
void Require(bool v) { if(!v) throw std::runtime_error("Contract check failed"); }
template<class F> void Reject(F fn) { bool rejected=false; try { fn(); } catch(const std::exception&) { rejected=true; } Require(rejected); }
int main() {
    Require(observer::Form("0x14")==20);
    Reject([]{observer::Form(-1);}); Reject([]{observer::Form(0);}); Reject([]{observer::Form("0x14garbage");});
    Reject([]{observer::Form(1.5);}); Reject([]{observer::Form(std::uint64_t(UINT32_MAX)+1);});
    auto r=observer::Parse(json{{"refs",{"0x14"}},{"nodes",{{{"ref","0x14"},{"name","Hand"},{"firstPerson",true}}}}});
    Require(r.refs.size()==1 && r.nodes[0].firstPerson);
    Reject([]{observer::Parse(json{{"refs",json(std::vector<int>(17,20))}});});
    Reject([]{observer::Parse(json{{"timeoutMs",4000}});});
    Reject([]{observer::Parse(json{{"timeoutMs",std::uint64_t(UINT32_MAX)+101}});});
    Reject([]{observer::Parse(json{{"nodes",{{{"ref",20},{"name","Hand"},{"firstPerson",1}}}}});});
    Reject([]{observer::Parse(json{{"physics",true}});});
    observer::Gate expired(100,2); Require(!expired.Begin(100,2,false));
    observer::Gate stale(100,2); Require(!stale.Begin(50,3,false));
    observer::Gate loading(100,2); Require(!loading.Begin(50,2,true));
    observer::Gate canceled(100,2); Require(canceled.Cancel()); Require(!canceled.Begin(50,2,false));
    observer::Gate running(100,2); Require(running.Begin(50,2,false)); Require(!running.Cancel()); running.Finish(); Require(running.Get()==observer::State::finished);
    Require(!observer::Finite(json{{"position",{std::numeric_limits<double>::infinity()}}}));
    std::cout << "Observer request bounds, identity, stale-load and deadline checks passed\n";
}
