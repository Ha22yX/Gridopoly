#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace gridopoly::pi {

// Admission never waits for capacity. A permit spans file validation and socket
// sending; releasing it does not refund the rate token. Control/legacy routes
// do not use this budget.
class DevelopmentAssetLimiter {
 public:
  using Clock = std::chrono::steady_clock;
  struct Stats {
    std::size_t active{}, peak{}, limit{};
    std::uint64_t admitted{}, rejected{};
  };
  class Permit {
   public:
    explicit Permit(DevelopmentAssetLimiter& owner) : owner_(owner) {}
    ~Permit() { owner_.release(); }
    Permit(const Permit&) = delete;
    Permit& operator=(const Permit&) = delete;
   private:
    DevelopmentAssetLimiter& owner_;
  };

  explicit DevelopmentAssetLimiter(std::size_t workers, Clock::time_point now = Clock::now())
      : limit_(std::min<std::size_t>(4, std::max<std::size_t>(1, workers / 2))),
        lastRefill_(now) {}

  std::shared_ptr<Permit> acquire(Clock::time_point now = Clock::now()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (now > lastRefill_) {
      tokens_ = std::min(4.0, tokens_ + std::chrono::duration<double>(now - lastRefill_).count() * 8.0);
      lastRefill_ = now;
    }
    if (active_ >= limit_ || tokens_ < 1.0) {
      ++rejected_;
      return {};
    }
    auto permit = std::make_shared<Permit>(*this);
    tokens_ -= 1.0;
    ++active_;
    ++admitted_;
    peak_ = std::max(peak_, active_);
    return permit;
  }

  Stats stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {active_, peak_, limit_, admitted_, rejected_};
  }

 private:
  void release() {
    std::lock_guard<std::mutex> lock(mutex_);
    --active_;
  }
  const std::size_t limit_;
  mutable std::mutex mutex_;
  Clock::time_point lastRefill_;
  double tokens_{4.0};
  std::size_t active_{}, peak_{};
  std::uint64_t admitted_{}, rejected_{};
};

}  // namespace gridopoly::pi
