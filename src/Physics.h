#pragma once
#include "Contract.h"
#include <atomic>
namespace observer::physics {
json Snapshot(const Request&,std::uint64_t,const std::atomic<std::uint64_t>&,const std::atomic<bool>&);
void Invalidate();
}
