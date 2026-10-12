//
// AsyncTaskQueue.hpp -- BMX synchronous shadow of CLK's Concurrency/AsyncTaskQueue.hpp.
//
// Upstream spawns a std::thread per queue; bare-metal newlib has no threads.
// All CLK BBC core objects on BMX live on the single emulator thread, so
// actions are performed inline at enqueue time, preserving serial ordering.
// lock_flush/spin_flush/stop/start become no-ops (nothing is ever pending).
// Same template signature and member API, so no vendored file is touched.
//
// Only AsyncTaskQueue<false> (void performer) is used on the BBC path
// (audio queue); the Performer path is preserved for API compatibility.
//

#pragma once

#include <concepts>
#include <functional>
#include <vector>

#include "ClockReceiver/TimeTypes.hpp"

namespace Concurrency {

template <typename Performer> struct TaskQueueStorage {
	template <typename... Args> TaskQueueStorage(Args&&... args) :
		performer(std::forward<Args>(args)...),
		last_fired_(Time::nanos_now()) {}

	Performer performer;

protected:
	void update() {
		const auto time_now = Time::nanos_now();
		performer.perform(time_now - last_fired_);
		last_fired_ = time_now;
	}

private:
	Time::Nanos last_fired_;
};

template <> struct TaskQueueStorage<void> {
	TaskQueueStorage() {}

protected:
	void update() {}
};

template <
	bool perform_automatically,
	bool start_immediately = true,
	typename Performer = void
>
class AsyncTaskQueue: public TaskQueueStorage<Performer> {
public:
	template <typename... Args> AsyncTaskQueue(Args&&... args) :
		TaskQueueStorage<Performer>(std::forward<Args>(args)...) {}

	template <typename FuncT>
	requires std::invocable<FuncT>
	void enqueue(FuncT &&action) {
		TaskQueueStorage<Performer>::update();
		action();
	}

	size_t size() const {
		return 0;
	}

	void perform() {
		static_assert(!perform_automatically);
	}

	void stop() {}

	void start() {
		static_assert(!start_immediately);
	}

	void lock_flush() {}
	void spin_flush() {}

	~AsyncTaskQueue() = default;

private:
	static constexpr size_t MaximumEnqueueActions = 1000;
};

}
