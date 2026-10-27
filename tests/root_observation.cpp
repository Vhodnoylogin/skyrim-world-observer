#include "RootObservation.h"
#include <stdexcept>
void Require(bool v) { if (!v) throw std::runtime_error("root observation contract"); }
int main() {
    observer::RootObservation roots;
    const auto first = roots.Observe(1, 20, 5, false, 1000);
    Require(first != 0 && roots.Observe(1, 20, 5, false, 1000) == first);
    Require(roots.Observe(1, 20, 5, true, 1000) != first);
    Require(roots.Observe(1, 20, 5, false, 0) == 0);
    const auto reloaded = roots.Observe(1, 20, 5, false, 1000);
    Require(reloaded > first);
    Require(roots.Observe(2, 20, 5, false, 1000) > reloaded);
    const auto next = roots.Observe(2, 20, 6, false, 1000);
    for (unsigned n = 100; n < 230; ++n) roots.Observe(2, n, n, false, 1000);
    Require(roots.Observe(2, 20, 6, false, 1000) > next);
}
