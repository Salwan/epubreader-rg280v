#include "platform/power.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;
namespace epub {
uint64_t powerClock() {
    timespec t{};
    if (clock_gettime(CLOCK_BOOTTIME, &t)) clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000 + static_cast<uint64_t>(t.tv_nsec) / 1000000;
}
bool PowerSnapshot::onBattery(uint64_t now) const {
    return fresh(now) && percent && *percent >= 0 && *percent <= 100 && source != PercentSource::Unknown
        && external == ExternalPower::Battery && charge != ChargeStatus::Charging && charge != ChargeStatus::Full;
}
bool canPoweroffAdam(const PowerSnapshot& snapshot, uint64_t now) {
    return snapshot.profile == "jz-battery/usb-charger" && snapshot.error.empty()
        && snapshot.onBattery(now) && *snapshot.percent <= 2;
}
std::string PowerSnapshot::label(uint64_t now) const {
    if (!fresh(now)) return sequence ? "BATTERY: STALE / REFRESHING" : "BATTERY: UNKNOWN";
    std::string text = "BATTERY: ";
    if (percent) text += (source == PercentSource::Estimated ? "~" : "") + std::to_string(*percent) + '%';
    else text += "UNKNOWN";
    if (external == ExternalPower::Conflict) return text + " / POWER CONFLICT";
    if (charge == ChargeStatus::Charging) return text + " / CHARGING";
    if (charge == ChargeStatus::Full) return text + " / FULL";
    if (external == ExternalPower::Connected) return text + (charge == ChargeStatus::NotCharging ? " / USB NOT CHARGING" : " / USB POWER");
    return text + (external == ExternalPower::Battery ? " / ON BATTERY" : " / POWER UNKNOWN");
}
namespace {
std::optional<std::string> attribute(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) { if (errno == ENOENT) return {}; throw std::runtime_error("Cannot read " + path); }
    char bytes[256]; ssize_t n;
    do { n = ::read(fd, bytes, sizeof(bytes)); } while (n < 0 && errno == EINTR);
    ::close(fd);
    if (n <= 0 || n == static_cast<ssize_t>(sizeof(bytes))) throw std::runtime_error("Invalid attribute " + path);
    std::string s(bytes, static_cast<size_t>(n));
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
std::optional<int64_t> number(const std::string& path) {
    const auto s = attribute(path); if (!s) return {};
    size_t end = 0; const auto value = std::stoll(*s, &end);
    if (end != s->size()) throw std::runtime_error("Malformed attribute " + path);
    return value;
}
}
PowerSnapshot SysfsPower::sample(uint64_t now) {
    PowerSnapshot s; s.sampled = now; s.sequence = ++sequence_;
    try {
        DIR* dir = ::opendir(root_.c_str());
        if (!dir) throw std::runtime_error("Power supplies unavailable");
        std::vector<std::string> batteries, inputs;
        try {
            unsigned count = 0;
            while (const auto* entry = ::readdir(dir)) {
                const std::string name = entry->d_name;
                if (name == "." || name == "..") continue;
                if (++count > 64) throw std::runtime_error("Too many power supplies");
                const auto type = attribute(root_ + '/' + name + "/type");
                if (type == "Battery") batteries.push_back(name);
                if (type == "USB" || type == "Mains" || type == "USB_C") inputs.push_back(name);
            }
        } catch (...) { ::closedir(dir); throw; }
        ::closedir(dir);
        if (batteries.size() != 1) throw std::runtime_error("Battery supply ambiguous/unavailable");
        const auto battery = batteries.front();
        std::string input;
        // These are alternative profiles, never cross-paired with each other.
        if (battery == "jz-battery" && std::find(inputs.begin(), inputs.end(), "usb-charger") != inputs.end()) input = "usb-charger";
        else if (battery == "battery" && std::find(inputs.begin(), inputs.end(), "usb") != inputs.end()) input = "usb";
        else if (battery != "battery" && battery != "jz-battery" && inputs.size() == 1) input = inputs.front();
        s.profile = battery + '/' + (input.empty() ? "unknown" : input);
        const auto path = root_ + '/' + battery + '/';
        const auto status = attribute(path + "status");
        if (status == "Charging") s.charge = ChargeStatus::Charging;
        else if (status == "Full") s.charge = ChargeStatus::Full;
        else if (status == "Discharging") s.charge = ChargeStatus::Discharging;
        else if (status == "Not charging") s.charge = ChargeStatus::NotCharging;
        else if (status && *status != "Unknown") throw std::runtime_error("Malformed charging status");
        if (!input.empty()) {
            const auto online = number(root_ + '/' + input + "/online");
            if (online && (*online == 0 || *online == 1)) s.external = *online ? ExternalPower::Connected : ExternalPower::Battery;
            else if (online) throw std::runtime_error("Invalid USB online value");
        }
        if (s.external == ExternalPower::Battery && (s.charge == ChargeStatus::Charging || s.charge == ChargeStatus::Full)) s.external = ExternalPower::Conflict;
        if (s.external == ExternalPower::Connected && s.charge == ChargeStatus::Discharging) s.external = ExternalPower::Conflict;
        if (inputs.size() > 1) s.external = ExternalPower::Unknown; // No unrelated supply guessing.
        if (s.profile != previousProfile_ || s.external != previous_ || s.charge != previousCharge_ || now < previousTime_ || now - previousTime_ > 6000) reset();
        previousProfile_ = s.profile; previous_ = s.external; previousCharge_ = s.charge; previousTime_ = now;
        s.microvolts = number(path + "voltage_now");
        if (s.microvolts && (*s.microvolts < 1000000 || *s.microvolts > 6000000)) throw std::runtime_error("Unsupported voltage units/range (expected uV)");
        if (const auto capacity = number(path + "capacity")) {
            if (*capacity < 0 || *capacity > 100) throw std::runtime_error("Invalid native capacity");
            s.percent = static_cast<int>(*capacity); s.source = PercentSource::Native; voltages_.clear();
        } else if (s.microvolts) {
            const auto low = number(path + "voltage_min_design"), high = number(path + "voltage_max_design");
            if (!low || !high || *low < 2000000 || *high > 5000000 || *low >= *high) throw std::runtime_error("Voltage design bounds unavailable/invalid");
            voltages_.push_back(*s.microvolts); if (voltages_.size() > 3) voltages_.erase(voltages_.begin());
            auto sorted = voltages_; std::sort(sorted.begin(), sorted.end());
            const auto voltage = sorted[sorted.size() / 2];
            s.percent = static_cast<int>(std::clamp((voltage - *low) * 100 / (*high - *low), int64_t{0}, int64_t{100}));
            s.source = PercentSource::Estimated;
        } else throw std::runtime_error("Capacity and voltage unavailable");
    } catch (const std::exception& e) {
        s.percent.reset(); s.source = PercentSource::Unknown; s.error = e.what(); reset();
    }
    return s;
}
PowerObserver::PowerObserver(std::function<PowerSnapshot()> read) : read_(std::move(read)), worker_(&PowerObserver::loop, this) {}
PowerObserver::~PowerObserver() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
    wake_.notify_one(); worker_.join();
}
PowerSnapshot PowerObserver::snapshot() { std::lock_guard<std::mutex> lock(mutex_); return latest_; }
void PowerObserver::refresh() {
    std::lock_guard<std::mutex> lock(mutex_); ++epoch_; latest_ = {}; refresh_ = true; wake_.notify_one();
}
void PowerObserver::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        const auto epoch = epoch_; refresh_ = false; lock.unlock();
        PowerSnapshot s;
        try { s = read_(); } catch (const std::exception& e) { s.error = e.what(); }
        lock.lock(); if (epoch == epoch_) latest_ = std::move(s);
        wake_.wait_for(lock, std::chrono::seconds(2), [&] { return stop_ || refresh_; });
    }
}
namespace {
class PosixPoweroffProcess : public PoweroffProcess {
    pid_t child_ = -1;
public:
    ~PosixPoweroffProcess() override {
        cancel();
        // A stuck sync helper must not turn final cleanup into an unbounded wait.
        if (child_ > 0) (void)::waitpid(child_, nullptr, WNOHANG);
    }
    std::string start(const std::string& executable, const std::vector<std::string>& argv) override {
        if (child_ > 0 && !poll()) return "Previous shutdown command not reaped";
        std::vector<char*> args; for (const auto& arg : argv) args.push_back(const_cast<char*>(arg.c_str())); args.push_back(nullptr);
        const int error = ::posix_spawn(&child_, executable.c_str(), nullptr, nullptr, args.data(), environ);
        if (error) { child_ = -1; return std::strerror(error); }
        return {};
    }
    std::optional<int> poll() override {
        if (child_ <= 0) return -1;
        int status = 0; const auto rc = ::waitpid(child_, &status, WNOHANG);
        if (!rc) return {};
        if (rc < 0 && errno == EINTR) return {};
        if (rc < 0) std::cerr << "POWER COMMAND WAIT ERROR pid=" << child_ << ' ' << std::strerror(errno) << '\n';
        child_ = -1;
        if (rc > 0 && WIFEXITED(status)) return WEXITSTATUS(status);
        if (rc > 0 && WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return -1;
    }
    void cancel() override { if (child_ > 0) ::kill(child_, SIGKILL); }
};
}
NormalPoweroff::NormalPoweroff(bool enabled, std::function<bool()> canSubmit, std::unique_ptr<PoweroffProcess> process)
    : enabled_(enabled), canSubmit_(std::move(canSubmit)), process_(std::move(process)) {
    if (enabled_ && !process_) process_ = std::make_unique<PosixPoweroffProcess>();
}
NormalPoweroff::~NormalPoweroff() = default;
void NormalPoweroff::spawn(uint64_t now) {
    if (!canSubmit_ || !canSubmit_()) {
        result_ = {PoweroffState::Failed, "Poweroff cancelled: battery or profile changed"};
        std::cout << "POWER COMMAND CANCELLED boot-ms=" << now << '\n'; return;
    }
    const char* path = stage_ == 0 ? "/bin/sync" : "/sbin/poweroff";
    std::cout << "POWER COMMAND " << path << " argv=[" << path << "]\n";
    const auto error = process_->start(path, {path});
    if (!error.empty()) {
        result_ = {PoweroffState::Failed, std::string(path) + ": " + error};
        std::cerr << "POWER COMMAND SPAWN ERROR " << result_.detail << '\n'; return;
    }
    since_ = now; result_ = {PoweroffState::Pending, path};
}
void NormalPoweroff::request(uint64_t now) {
    if (result_.state == PoweroffState::Pending) return;
    if (!enabled_) { result_ = {PoweroffState::Unsupported, "System shutdown unavailable in this runtime"}; return; }
    stage_ = 0; spawn(now);
}
PoweroffResult NormalPoweroff::poll(uint64_t now) {
    if (result_.state != PoweroffState::Pending) return result_;
    if (const auto exit = process_->poll()) {
        const std::string path = stage_ == 0 ? "/bin/sync" : "/sbin/poweroff";
        std::cout << "POWER COMMAND RESULT " << path << " exit=" << *exit << '\n';
        if (*exit) result_ = {PoweroffState::Failed, path + " exited with status " + std::to_string(*exit)};
        else if (stage_++ == 0) spawn(now);
        else result_ = {PoweroffState::Submitted, "Shutdown request submitted"};
    } else if (now < since_ || now - since_ >= 2000) {
        process_->cancel();
        result_ = {PoweroffState::TimedOut, "Normal shutdown command timed out"};
        std::cerr << "POWER COMMAND TIMEOUT " << (stage_ == 0 ? "/bin/sync" : "/sbin/poweroff") << " elapsed-ms=" << now - since_ << '\n';
    }
    return result_;
}
}
