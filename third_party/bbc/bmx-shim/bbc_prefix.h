//
// bbc_prefix.h -- force-included prefix for vendored CLK (BBC Micro) objects.
//
// Bare-metal newlib is built without threading support, so libstdc++
// provides <mutex>/<condition_variable> headers but no working
// std::mutex / std::condition_variable. All CLK BBC core objects on BMX
// live on the single emulator thread (the menu pump runs on that same
// thread while open), so no-op stand-ins are correct here. The only real
// thread user (Concurrency::AsyncTaskQueue) is shadowed by a synchronous
// implementation in bmx-shim/Concurrency/AsyncTaskQueue.hpp, so nothing
// ever blocks on these. Pattern mirrors third_party/stella/bmx-shim.
//

#ifndef BMX_BBC_PREFIX_H
#define BMX_BBC_PREFIX_H

#include <chrono>
#include <cstddef>

namespace std {

class mutex {
 public:
  void lock() {}
  bool try_lock() { return true; }
  void unlock() {}
  typedef mutex* native_handle_type;
  native_handle_type native_handle() { return nullptr; }
};

enum class cv_status { no_timeout, timeout };

// Never exercised on BMX: AsyncTaskQueue is synchronous (see bmx-shim
// shadow), so nothing ever waits. Present only so headers that name
// condition_variable compile.
class condition_variable {
 public:
  void notify_one() {}
  void notify_all() {}
  template <typename Lock>
  void wait(Lock&) {}
  template <typename Lock, typename Pred>
  bool wait(Lock&, Pred pred) {
    return pred();
  }
  template <typename Lock, typename Clock, typename Duration>
  cv_status wait_until(Lock&, const std::chrono::time_point<Clock, Duration>&) {
    return cv_status::timeout;
  }
};

}  // namespace std

#endif  // BMX_BBC_PREFIX_H
