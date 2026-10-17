#include "app/power_policy.hpp"
namespace epub {
void PowerPolicy::invalidate() { low_ = critical_ = recoverLow_ = recoverCritical_ = 0; }
PowerEffect PowerPolicy::observe(const PowerSnapshot& s, uint64_t now, bool reading) {
    if (!s.fresh(now)) { invalidate(); return PowerEffect::None; }
    if (s.sequence == sequence_ && s.sampled == sampled_) return PowerEffect::None;
    if (s.sampled < sampled_ || s.sampled - sampled_ > 6000) invalidate();
    sequence_ = s.sequence; sampled_ = s.sampled;
    if (s.external == ExternalPower::Connected || s.charge == ChargeStatus::Charging || s.charge == ChargeStatus::Full) {
        warning = false; invalidate();
        if (s.external == ExternalPower::Connected) lowLatched_ = criticalLatched_ = criticalPending_ = false;
        return PowerEffect::None;
    }
    if (!s.onBattery(now)) { warning = false; invalidate(); return PowerEffect::None; }
    warning = *s.percent < 15;
    recoverLow_ = *s.percent >= 7 ? (recoverLow_ < 2 ? recoverLow_ + 1 : 2) : 0;
    recoverCritical_ = *s.percent >= 4 ? (recoverCritical_ < 2 ? recoverCritical_ + 1 : 2) : 0;
    if (recoverLow_ >= 2) lowLatched_ = false;
    if (recoverCritical_ >= 2) criticalLatched_ = criticalPending_ = false;
    low_ = *s.percent <= 5 ? (low_ < 2 ? low_ + 1 : 2) : 0;
    critical_ = *s.percent <= 2 ? (critical_ < 2 ? critical_ + 1 : 2) : 0;
    if (critical_ >= 2 && !criticalLatched_ && !criticalPending_) { criticalPending_ = true; lowLatched_ = true; return PowerEffect::Poweroff; }
    if (low_ >= 2 && !lowLatched_ && reading) { lowLatched_ = true; return PowerEffect::ReturnToList; }
    return PowerEffect::None;
}
}
