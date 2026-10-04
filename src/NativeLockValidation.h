#pragma once
#include <cstdint>
namespace observer::physics {
struct LockWords {std::uint32_t writer=0,count=0;};
static_assert(sizeof(LockWords)==8);
template<class TryRead,class TryWrite,class UnlockRead,class UnlockWrite>
bool ValidateNativeLocks(TryRead read,TryWrite write,UnlockRead unRead,UnlockWrite unWrite,std::uint32_t thread) {
    LockWords lock;
    if(!read(&lock))return false;
    const auto readGood=lock.count==1;unRead(&lock);
    if(!readGood || lock.count!=0)return false;
    if(!write(&lock))return false;
    const auto writeGood=lock.count==0x80000001u && lock.writer==thread;unWrite(&lock);
    if(!writeGood || lock.count!=0 || lock.writer!=0)return false;
    // The engine permits reentrant write/read ownership on the owning thread.
    if(!write(&lock))return false;
    if(!write(&lock)){unWrite(&lock);return false;}
    const auto nestedGood=lock.count==0x80000002u;unWrite(&lock);unWrite(&lock);
    return nestedGood && lock.count==0 && lock.writer==0;
}
}
