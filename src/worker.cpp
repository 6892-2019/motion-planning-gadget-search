#include "precompiled.hpp"
#include "worker.hpp"

void worker_thread(int core_number, bounded_queue<std::function<void()>>& queue) {
	//TODO: pin thread to core
	while (true)
		queue.take()();
}