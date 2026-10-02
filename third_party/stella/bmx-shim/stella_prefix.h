//
// stella_prefix.h -- force-included prefix for vendored Stella objects.
//
// Bare-metal newlib is built without threading support, so libstdc++
// provides <mutex> but no std::mutex (lock_guard/unique_lock/scoped_lock
// are all present). All Stella core objects on BMX live on the single
// emulator thread (the menu pump runs on that same thread while open),
// so a no-op mutex is correct here. This stand-in satisfies Event.hxx,
// Logger.hxx, AudioQueue.hxx and friends without touching upstream files.
//

#ifndef BMX_STELLA_PREFIX_H
#define BMX_STELLA_PREFIX_H

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

// Never exercised on BMX: the only timer source (EventHandler held-event
// clear) is compiled out below, so the worker thread never starts and
// nothing ever waits. Present only so TimerManager.hxx compiles.
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

#endif  // BMX_STELLA_PREFIX_H
