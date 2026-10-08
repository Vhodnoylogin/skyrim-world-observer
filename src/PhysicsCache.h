#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
namespace observer::physics {
struct Contact {
    std::uint64_t sequence=0, generation=0, world=0;
    std::int64_t timeNs=0;
    std::uint32_t bodyA=0,bodyB=0;
    std::array<float,3> position{},normal{};
    float separation=0,separatingVelocity=0;
    std::uint8_t flags=0;
    bool velocityAvailable=false,flagsAvailable=false;
    int callbackType=0;
};
// Protected externally. The physics callback never waits for this cache's mutex.
template<std::size_t Capacity=256> class Cache {
public:
    bool SameBodies(std::span<const std::uint32_t> bodies)const {
        if(bodies.size()!=watched.size())return false;
        for(auto id:bodies){bool found=false;for(auto v:watched)found|=v==id;if(!found)return false;}
        return true;
    }
    void StartBounded(std::span<const std::uint32_t> bodies,std::uint64_t gen,std::int64_t now,std::int64_t duration) {
        if(bodies.empty() || bodies.size()>64 || duration<100000000LL || duration>60000000000LL)throw std::invalid_argument("Invalid bounded capture");
        watched.assign(bodies.begin(),bodies.end());generation=gen;startNs=now;untilNs=now+duration;++epoch;
    }
    const std::vector<std::uint32_t>& Watched()const{return watched;}
    void Arm(std::span<const std::uint32_t> bodies,std::uint64_t gen,std::int64_t now) {
        bool changed=bodies.size()!=watched.size();
        for(auto id:bodies){bool found=false;for(auto v:watched)found|=v==id;changed|=!found;}
        if(gen!=generation || now>=untilNs || changed) { watched.clear(); startNs=now; ++epoch; }
        generation=gen; untilNs=now+5000000000LL;
        for(auto id:bodies) {
            bool present=false; for(auto v:watched) present|=v==id;
            if(!present && watched.size()<64) watched.push_back(id);
        }
    }
    bool Interested(std::uint32_t a,std::uint32_t b,std::uint64_t gen,std::int64_t now) const {
        if(gen!=generation || now<startNs || now>=untilNs) return false;
        for(auto v:watched) if(v==a || v==b) return true;
        return false;
    }
    void Push(Contact c) { c.sequence=++latest; ring[(latest-1)%Capacity]=c; }
    std::vector<Contact> Since(std::uint64_t cursor,std::uint64_t gen,std::uint64_t world) const {
        std::vector<Contact> out;
        const auto first=Oldest();
        for(auto s=first;s<=latest && s>0;++s) {
            const auto& c=ring[(s-1)%Capacity];
            if(c.sequence>cursor && c.generation==gen && c.world==world && c.timeNs>=startNs && c.timeNs<untilNs) out.push_back(c);
        }
        return out;
    }
    std::uint64_t Oldest() const { return latest>Capacity?latest-Capacity+1:(latest?1:0); }
    bool Gap(std::uint64_t cursor) const { return Oldest()>0 && cursor<Oldest()-1; }
    void Invalidate() { untilNs=0; watched.clear(); }
    std::uint64_t generation=0,latest=0,epoch=0;
    std::int64_t startNs=0,untilNs=0;
private:
    std::array<Contact,Capacity> ring{};
    std::vector<std::uint32_t> watched;
};
}
