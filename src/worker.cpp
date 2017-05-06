#include "precompiled.hpp"
#include "worker.hpp"
#include "pinning.hpp"

void worker_thread(int core_number, bounded_queue<std::function<void()>>& queue) {
	pin_this_thread(core_number);
	while (true)
		queue.take()();
}