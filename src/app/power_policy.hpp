#pragma once
#include "platform/power.hpp"
namespace epub {
enum class PowerEffect { None, ReturnToList, Poweroff };
class PowerPolicy {
    uint32_t sequence_ = 0;
    uint64_t sampled_ = 0;
    unsigned low_ = 0, critical_ = 0, recoverLow_ = 0, recoverCritical_ = 0;
    bool lowLatched_ = false, criticalLatched_ = false, criticalPending_ = false;
public:
    bool warning = false;
    PowerEffect observe(const PowerSnapshot& snapshot, uint64_t now, bool reading);
    void invalidate();
    void poweroffAttempted() { criticalPending_ = false; criticalLatched_ = true; }
    void cancelPoweroff() { criticalPending_ = false; critical_ = 0; }
};
}
