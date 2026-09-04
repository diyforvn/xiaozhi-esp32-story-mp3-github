#ifndef IDLE_ANIMATION_REGISTRY_H
#define IDLE_ANIMATION_REGISTRY_H

#include <cstdint>
#include <cstddef>


struct IdleAnimationAsset {
    const char* name;
    const uint8_t* start;
    const uint8_t* end;
};

extern const IdleAnimationAsset kIdleAnimations[];
extern const size_t kIdleAnimationsCount;

#endif // IDLE_ANIMATION_REGISTRY_H