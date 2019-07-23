#include "precompiled.hpp"
#include "task_parallel.hpp"
#include <atomic>
#include <future>

void task_parallel(unsigned int threads, function_view<void()>* tasks, std::size_t task_count) {
	std::atomic<std::size_t> task_index_dispenser(0);
	std::vector<std::future<void>> futures;
	for (std::size_t i = 0; i < threads && i < task_count; ++i)
		futures.push_back(std::async(std::launch::async, [&]() {
			for (std::size_t index = task_index_dispenser++; index < task_count; index = task_index_dispenser++)
				tasks[index]();
		}));
	for (std::size_t i = 0; i < futures.size(); ++i)
		futures[i].wait();
}