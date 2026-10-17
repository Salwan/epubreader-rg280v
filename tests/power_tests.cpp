#include "app/power_policy.hpp"
#include "storage/store.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace epub;
void check(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
PowerSnapshot reading(int percent, uint32_t sequence, uint64_t time = 1000) {
    PowerSnapshot s; s.percent = percent; s.source = PercentSource::Native; s.external = ExternalPower::Battery; s.sequence = sequence; s.sampled = time; return s;
}
void policy() {
    PowerPolicy p;
    check(p.observe(reading(15, 1), 1000, true) == PowerEffect::None && !p.warning, "15% boundary");
    p.observe(reading(14, 2), 1000, true); check(p.warning, "14% warning");
    auto low = reading(5, 3);
    check(p.observe(low, 1000, true) == PowerEffect::None, "One low sample acted");
    for (int n = 0; n < 30; ++n) check(p.observe(low, 1000, true) == PowerEffect::None, "UI ticks counted as samples");
    check(p.observe(reading(5, 4), 1000, true) == PowerEffect::ReturnToList, "Two low samples did not act");
    check(p.observe(reading(3, 5), 1000, true) == PowerEffect::None, "Explicit reopen reclosed");
    check(p.observe(reading(2, 6), 1000, false) == PowerEffect::None, "One critical sample acted");
    check(p.observe(reading(0, 7), 1000, false) == PowerEffect::Poweroff, "App-wide zero/skipped critical threshold");
    check(p.observe(reading(1, 8), 1000, true) == PowerEffect::None, "Critical command loop");
    p.observe(reading(7, 9), 1000, true); p.observe(reading(7, 10), 1000, true);
    p.observe(reading(5, 11), 1000, true);
    check(p.observe(reading(5, 12), 1000, true) == PowerEffect::ReturnToList, "Low re-arm");
    p.observe(reading(2, 13), 1000, false);
    check(p.observe(reading(2, 14), 1000, false) == PowerEffect::Poweroff, "Critical re-arm");
    auto usb = reading(1, 15); usb.external = ExternalPower::Connected;
    p.observe(usb, 1000, true); check(!p.warning, "USB warning suppression");
    p.observe(reading(1, 16), 1000, false);
    check(p.observe(reading(1, 17), 1000, false) == PowerEffect::Poweroff, "USB episode re-arm");
    for (auto state : {ExternalPower::Unknown, ExternalPower::Conflict, ExternalPower::Connected}) {
        PowerPolicy q; auto s = reading(0, 1); s.external = state;
        q.observe(s, 1000, true); ++s.sequence;
        check(q.observe(s, 1000, true) == PowerEffect::None && !q.warning, "Unconfirmed battery operation acted");
    }
    for (auto state : {ChargeStatus::Charging, ChargeStatus::Full}) {
        PowerPolicy q; auto s = reading(0, 1); s.charge = state;
        q.observe(s, 1000, true); ++s.sequence;
        check(q.observe(s, 1000, true) == PowerEffect::None && !q.warning, "Charge suppression");
    }
    PowerPolicy q;
    q.observe(reading(16, 1), 1000, true); q.observe(reading(1, 2), 1000, true);
    check(q.observe(reading(1, 3), 1000, true) == PowerEffect::Poweroff, "16 -> 1 jump precedence");
    PowerPolicy stale;
    auto s = reading(2, 1); stale.observe(s, 1000, true);
    stale.observe(s, 7001, true); check(stale.warning, "Brief stale warning should be retainable");
    check(stale.observe(reading(2, 2, 7002), 7002, true) == PowerEffect::None, "Stale gap counted confirmation");
    stale.invalidate(); check(stale.observe(reading(2, 3, 7003), 7003, true) == PowerEffect::None, "Suspend retained confirmation");
    PowerPolicy wrap;
    wrap.observe(reading(5, 1, 0xfffffff0ULL), 0xfffffff0ULL, true);
    check(wrap.observe(reading(5, 2, 0x100000010ULL), 0x100000010ULL, true) == PowerEffect::ReturnToList, "SDL tick wrap affected power clock");
    PowerPolicy noise;
    for (uint32_t n = 1; n < 20; ++n) check(noise.observe(reading(n % 2 ? 5 : 6, n), 1000, true) == PowerEffect::None, "Noise triggered closure");
    auto invalid = reading(-1, 20); check(!invalid.onBattery(1000), "Invalid injected percent accepted");
    invalid.percent.reset(); check(!invalid.onBattery(1000), "Unknown became zero");
    PowerPolicy episodes;
    episodes.observe(reading(2, 1), 1000, true); episodes.observe(reading(2, 2), 1000, true);
    episodes.observe(reading(4, 3), 1000, true); episodes.observe(reading(4, 4), 1000, true);
    episodes.observe(reading(2, 5), 1000, true);
    check(episodes.observe(reading(2, 6), 1000, true) == PowerEffect::Poweroff, "Exact 4% critical re-arm");
    invalid = reading(7, 7); invalid.percent.reset(); episodes.observe(invalid, 1000, true); invalid.sequence = 8; episodes.observe(invalid, 1000, true);
    check(episodes.observe(reading(5, 9), 1000, true) == PowerEffect::None && episodes.observe(reading(5, 10), 1000, true) == PowerEffect::None, "Unknown data re-armed low episode");
    PowerPolicy cancelled;
    cancelled.observe(reading(1, 1), 1000, false); cancelled.observe(reading(1, 2), 1000, false);
    cancelled.cancelPoweroff(); cancelled.observe(reading(1, 3), 1000, false);
    check(cancelled.observe(reading(1, 4), 1000, false) == PowerEffect::Poweroff, "Cancelled unsubmitted request latched as an actual attempt");
    cancelled.poweroffAttempted();
    check(cancelled.observe(reading(1, 5), 1000, false) == PowerEffect::None, "Actual attempt did not latch");
}
void adamShutdownGuard() {
    auto s = reading(2, 1); s.profile = "jz-battery/usb-charger";
    check(canPoweroffAdam(s, 1000), "Confirmed Adam critical profile was not enabled");
    for (int percent : {0, 1, 2, 3, 5, 100, -1, 101}) {
        s.percent = percent;
        check(canPoweroffAdam(s, 1000) == (percent >= 0 && percent <= 2), "Final Adam percentage recheck");
    }
    s.percent = 1; s.source = PercentSource::Estimated;
    check(canPoweroffAdam(s, 1000) && !canPoweroffAdam(s, 7001) && !canPoweroffAdam(s, 999), "Final Adam freshness recheck");
    for (const auto external : {ExternalPower::Connected, ExternalPower::Unknown, ExternalPower::Conflict}) {
        s.external = external; check(!canPoweroffAdam(s, 1000), "Final Adam USB/conflict suppression");
    }
    s.external = ExternalPower::Battery;
    for (const auto charge : {ChargeStatus::Charging, ChargeStatus::Full}) {
        s.charge = charge; check(!canPoweroffAdam(s, 1000), "Final Adam charging/full suppression");
    }
    s.charge = ChargeStatus::Unknown;
    for (const auto* profile : {"battery/usb", "jz-battery/unknown", "", "other/charger"}) {
        s.profile = profile; check(!canPoweroffAdam(s, 1000), "Unverified supply profile enabled normal shutdown");
    }
    s.profile = "jz-battery/usb-charger"; s.error = "read failed";
    check(!canPoweroffAdam(s, 1000), "Errored snapshot reached normal shutdown");
    s.error.clear(); s.percent.reset(); check(!canPoweroffAdam(s, 1000), "Unknown percent reached normal shutdown");
}
void put(const std::string& root, const std::string& name, const std::string& value) {
    const auto path = std::filesystem::path(root) / name; std::filesystem::create_directories(path.parent_path()); std::ofstream(path) << value << '\n';
}
void observedAdam(const std::string& root) {
    // User reports: power-probe-19700101-071205-420.txt (unplugged) and
    // power-probe-19700101-072002-536.txt (USB connected). No capacity/status nodes.
    put(root, "jz-battery/type", "Battery");
    put(root, "jz-battery/voltage_now", "3966000");
    put(root, "jz-battery/voltage_min_design", "3400000");
    put(root, "jz-battery/voltage_max_design", "4200000");
    put(root, "usb-charger/type", "USB"); put(root, "usb-charger/online", "0");
    SysfsPower source(root); const auto s = source.sample(1000);
    check(s.error.empty() && s.profile == "jz-battery/usb-charger" && s.microvolts == 3966000,
          "Observed Adam supply discovery/voltage failed");
    check(s.percent == 70 && s.source == PercentSource::Estimated && s.onBattery(1000)
          && s.charge == ChargeStatus::Unknown && s.label(1000) == "BATTERY: ~70% / ON BATTERY",
          "Observed Adam report invented capacity/charge status or miscalculated estimate");
    source.sample(3000); // Fill the battery-only history before changing power state.
    put(root, "usb-charger/online", "1"); put(root, "jz-battery/voltage_now", "4122000");
    const auto connected = source.sample(5000);
    check(connected.error.empty() && connected.microvolts == 4122000 && connected.percent == 90
          && connected.source == PercentSource::Estimated && connected.external == ExternalPower::Connected
          && connected.charge == ChargeStatus::Unknown && !connected.onBattery(5000)
          && connected.label(5000) == "BATTERY: ~90% / USB POWER",
          "Observed connected Adam report blended battery-only samples or invented charging status");
    put(root, "usb-charger/online", "0"); put(root, "jz-battery/voltage_now", "3416000");
    const auto critical = source.sample(7000);
    check(critical.percent == 2 && canPoweroffAdam(critical, 7000), "Confirmed voltage profile did not reach the production critical guard");
}
void parsing(const std::string& root) {
    namespace fs = std::filesystem;
    put(root, "jz-battery/type", "Battery"); put(root, "usb-charger/type", "USB"); put(root, "usb-charger/online", "0");
    put(root, "jz-battery/voltage_now", "3800000"); put(root, "jz-battery/voltage_min_design", "3400000"); put(root, "jz-battery/voltage_max_design", "4200000");
    SysfsPower source(root);
    auto s = source.sample(1000); check(s.percent == 50 && s.source == PercentSource::Estimated && s.onBattery(1000), "Kernel-design voltage estimate");
    put(root, "jz-battery/voltage_now", "4100000"); source.sample(3000);
    put(root, "jz-battery/voltage_now", "3810000"); s = source.sample(5000); check(s.percent == 51, "Median-three smoothing");
    put(root, "usb-charger/online", "1"); put(root, "jz-battery/voltage_now", "4200000");
    s = source.sample(7000); check(s.percent == 100 && s.charge == ChargeStatus::Unknown && s.label(7000).find("USB POWER") != std::string::npos, "USB-only status/median reset");
    put(root, "jz-battery/status", "Full"); s = source.sample(9000); check(s.label(9000).find("FULL") != std::string::npos, "Full status");
    put(root, "jz-battery/status", "Not charging"); s = source.sample(11000); check(s.label(11000).find("NOT CHARGING") != std::string::npos, "Not charging status");
    put(root, "jz-battery/status", "Discharging"); s = source.sample(13000); check(s.external == ExternalPower::Conflict, "Conflicting status");
    put(root, "usb-charger/online", "0"); put(root, "jz-battery/capacity", "64"); s = source.sample(15000);
    check(s.percent == 64 && s.source == PercentSource::Native && s.label(15000).find('~') == std::string::npos, "Native preference");
    for (auto bad : {"101", "-1", "NaN", "50%", "999999999999999999999999999999"}) {
        put(root, "jz-battery/capacity", bad); s = source.sample(17000); check(!s.percent && !s.error.empty(), "Malformed capacity invented percentage");
    }
    fs::remove(root + "/jz-battery/capacity"); fs::remove(root + "/jz-battery/status");
    for (auto bad : {"3.8", "3800", "garbage", "9000000"}) {
        put(root, "jz-battery/voltage_now", bad); check(!source.sample(19000).percent, "Unsupported units accepted");
    }
    put(root, "jz-battery/voltage_now", "3300000"); s = source.sample(21000); check(s.percent == 0, "Physical below-minimum voltage is not a read failure");
    put(root, "jz-battery/voltage_now", "4200000"); s = source.sample(31000); check(s.percent == 100, "Suspend gap blended voltage estimates");
    put(root, "jz-battery/voltage_min_design", "4200000"); check(!source.sample(23000).percent, "Inconsistent design bounds");
    fs::remove_all(root); put(root, "battery/type", "Battery"); put(root, "battery/capacity", "2"); put(root, "usb/type", "USB"); put(root, "usb/online", "0");
    s = source.sample(25000); check(s.percent == 2 && s.profile == "battery/usb" && s.onBattery(25000), "Alternate supply names");
    fs::rename(root + "/usb", root + "/usb-charger"); s = source.sample(27000); check(!s.onBattery(27000), "Mixed unrelated profile pair");
    put(root, "second/type", "Battery"); check(!source.sample(29000).percent, "Ambiguous batteries");
    fs::remove_all(root); check(!source.sample(31000).percent, "Missing sysfs became zero");
    check(s.label(40000).find("STALE") != std::string::npos, "Stale field");
}
struct Process : PoweroffProcess {
    std::vector<std::string> calls;
    std::string error;
    std::optional<int> exit = 0;
    bool cancelled = false;
    std::string start(const std::string& path, const std::vector<std::string>& args) override {
        check(args == std::vector<std::string>{path}, "Poweroff argv not fixed"); calls.push_back(path); return error;
    }
    std::optional<int> poll() override { return exit; }
    void cancel() override { cancelled = true; }
};
void backend() {
    NormalPoweroff unavailable; unavailable.request(0); check(unavailable.poll(1).state == PoweroffState::Unsupported, "Default backend can shutdown host");
    auto proc = std::make_unique<Process>(); auto* p = proc.get(); bool allowed = true;
    NormalPoweroff backend(true, [&] { return allowed; }, std::move(proc));
    backend.request(1000); check(p->calls == std::vector<std::string>{"/bin/sync"}, "Sync ordering");
    check(backend.poll(1001).state == PoweroffState::Pending && p->calls.back() == "/sbin/poweroff", "Normal poweroff ordering");
    check(backend.poll(1002).state == PoweroffState::Submitted, "Command result claims missing submission");
    backend.request(2000); allowed = false; backend.poll(2001);
    check(p->calls.size() == 3 && backend.poll(2002).state == PoweroffState::Failed, "Charger inserted between sync/poweroff");
    allowed = true; p->error = "Permission denied"; backend.request(3000); check(backend.poll(3001).state == PoweroffState::Failed, "Spawn permission error");
    p->error.clear(); p->exit = 1; backend.request(4000); check(backend.poll(4001).state == PoweroffState::Failed, "Nonzero exit");
    p->exit.reset(); backend.request(5000); check(backend.poll(6999).state == PoweroffState::Pending, "Premature timeout");
    check(backend.poll(7000).state == PoweroffState::TimedOut && p->cancelled, "Unbounded command wait");
    auto sample = reading(1, 1); sample.profile = "jz-battery/usb-charger";
    auto guardedProcess = std::make_unique<Process>(); const auto* recorded = guardedProcess.get();
    NormalPoweroff guarded(true, [&] { return canPoweroffAdam(sample, 1000); }, std::move(guardedProcess));
    guarded.request(1000); sample.external = ExternalPower::Connected;
    check(guarded.poll(1001).state == PoweroffState::Failed && recorded->calls == std::vector<std::string>{"/bin/sync"},
          "Production guard submitted poweroff after charger insertion during sync");
}
void observer() {
    std::atomic<unsigned> reads{0}; std::atomic<bool> release{false};
    PowerObserver observer([&] {
        const auto n = ++reads;
        if (n == 1) while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return reading(n == 1 ? 1 : 80, n, powerClock());
    });
    while (!reads.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto start = std::chrono::steady_clock::now(); check(!observer.snapshot().percent, "In-flight observer blocks/guesses");
    observer.refresh(); release = true;
    const auto deadline = start + std::chrono::seconds(2);
    while (observer.snapshot().percent != 80 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(observer.snapshot().percent == 80, "Refresh published pre-suspend result");
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "isolated root required"); std::filesystem::remove_all(argv[1]);
        policy(); adamShutdownGuard(); parsing(argv[1]); observedAdam(std::string(argv[1]) + "/observed-adam"); backend(); observer();
        std::cout << "PASS: power parsing, median/freshness, threshold episodes, observer refresh, isolated normal-shutdown adapter\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
