#pragma once
#include <RE/Skyrim.h>
#include <cstddef>
namespace observer {
static_assert(offsetof(RE::LOADED_REF_DATA,data3D)==0x68);
inline RE::NiAVObject* SceneRoot(RE::TESObjectREFR* ref,bool firstPerson=false){
    if(!ref || ref->IsDeleted())return nullptr;
    if(firstPerson)return ref->Get3D(true);
    // The typed loaded scene is independently documented by both pinned
    // CommonLib3.7 and SKSEVR2.0.12; preserve explicit first-person selection.
    auto loaded=ref->loadedData?ref->loadedData->data3D.get():nullptr;
    return loaded?loaded:ref->Get3D(false);
}
}
