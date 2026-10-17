#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace epub {
uint64_t powerClock(); // CLOCK_BOOTTIME: includes time spent suspended, no SDL tick wrap.
enum class PercentSource { Unknown, Native, Estimated };
enum class ExternalPower { Unknown, Battery, Connected, Conflict };
enum class ChargeStatus { Unknown, Discharging, Charging, Full, NotCharging };
struct PowerSnapshot {
    std::optional<int> percent;
    PercentSource source = PercentSource::Unknown;
    std::optional<int64_t> microvolts;
    ExternalPower external = ExternalPower::Unknown;
    ChargeStatus charge = ChargeStatus::Unknown;
    uint64_t sampled = 0;
    uint32_t sequence = 0;
    std::string profile, error;
    bool fresh(uint64_t now) const { return sequence && now >= sampled && now - sampled <= 6000; }
    bool onBattery(uint64_t now) const;
    std::string label(uint64_t now) const;
};
// Confirmed RG280V/Adam supply pair. Called again before each firmware command.
bool canPoweroffAdam(const PowerSnapshot& snapshot, uint64_t now);
// All filesystem work belongs to the observer thread. Root injection is for fixtures.
class SysfsPower {
    std::string root_, previousProfile_;
    ExternalPower previous_ = ExternalPower::Unknown;
    ChargeStatus previousCharge_ = ChargeStatus::Unknown;
    std::vector<int64_t> voltages_;
    uint64_t previousTime_ = 0;
    uint32_t sequence_ = 0;
public:
    explicit SysfsPower(std::string root) : root_(std::move(root)) {}
    PowerSnapshot sample(uint64_t now);
    void reset() { voltages_.clear(); previousTime_ = 0; }
};
class PowerObserver {
    std::function<PowerSnapshot()> read_;
    std::mutex mutex_;
    std::condition_variable wake_;
    PowerSnapshot latest_;
    bool stop_ = false, refresh_ = true;
    unsigned epoch_ = 0;
    std::thread worker_;
    void loop();
public:
    explicit PowerObserver(std::function<PowerSnapshot()> read);
    ~PowerObserver();
    PowerSnapshot snapshot();
    void refresh(); // Invalidates the mailbox and any in-flight pre-suspend result.
};
enum class PoweroffState { Idle, Pending, Submitted, Unsupported, Failed, TimedOut };
inline const char* poweroffStateName(PoweroffState state) {
    switch (state) {
    case PoweroffState::Idle: return "idle";
    case PoweroffState::Pending: return "pending";
    case PoweroffState::Submitted: return "submitted";
    case PoweroffState::Unsupported: return "unsupported";
    case PoweroffState::Failed: return "failed";
    case PoweroffState::TimedOut: return "timed-out";
    }
    return "unknown";
}
struct PoweroffResult { PoweroffState state = PoweroffState::Idle; std::string detail; };
class PoweroffPort {
public:
    virtual ~PoweroffPort() = default;
    virtual void request(uint64_t now) = 0;
    virtual PoweroffResult poll(uint64_t now) = 0;
};
class PoweroffProcess {
public:
    virtual ~PoweroffProcess() = default;
    virtual std::string start(const std::string& executable, const std::vector<std::string>& argv) = 0;
    virtual std::optional<int> poll() = 0; // nullopt: running; 0: success; other: failure.
    virtual void cancel() = 0;
};
// Fixed /bin/sync -> /sbin/poweroff argv, never a shell or forced shutdown.
// Disabled by default; the device runtime enables the confirmed Adam backend.
class NormalPoweroff : public PoweroffPort {
    bool enabled_;
    int stage_ = 0;
    uint64_t since_ = 0;
    PoweroffResult result_;
    std::function<bool()> canSubmit_;
    std::unique_ptr<PoweroffProcess> process_;
    void spawn(uint64_t now);
public:
    explicit NormalPoweroff(bool enabled = false, std::function<bool()> canSubmit = {}, std::unique_ptr<PoweroffProcess> process = {});
    ~NormalPoweroff() override;
    void request(uint64_t now) override;
    PoweroffResult poll(uint64_t now) override;
};
}
