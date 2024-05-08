#ifndef PERF_DATA_CONVERTER_SRC_QUIPPER_COMPAT_CLEANUP_H_
#define PERF_DATA_CONVERTER_SRC_QUIPPER_COMPAT_CLEANUP_H_

#include <functional>
#include <memory>

namespace quipper {
namespace compat {

// Helper to RAII invoke a callback at the end of a scope.
// Simlar to absl::Cleanup, or Golang's defer().
class Cleanup {
 public:
  explicit Cleanup(std::function<void()> callback)
      : callback_(std::move(callback)) {}

  ~Cleanup() {
    if (callback_ != nullptr) {
      callback_();
    }
  }

  Cleanup() = delete;
  Cleanup(const Cleanup&) = delete;

 private:
  std::function<void()> callback_;
};

}  // namespace compat
}  // namespace quipper

#endif  // PERF_DATA_CONVERTER_SRC_QUIPPER_COMPAT_CLEANUP_H_
