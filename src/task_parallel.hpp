#ifndef TASK_PARALLEL_HPP
#define TASK_PARALLEL_HPP

#include "function_view.hpp"

void task_parallel(unsigned int threads, function_view<void()>* tasks, std::size_t task_count);

//Perhaps in the future we could return a std::tuple of the results?  But we'd
//have to replace void-returning with std::monostate or some other well-behaved
//default type.
template<class ...Callable>
void task_parallel(unsigned int threads, Callable&&... callables) {
	if (threads <= 1)
		(std::forward<Callable>(callables)(), ...); //comma fold
	else {
		function_view<void()> tasks[] = {function_view<void()>(callables)...};
		task_parallel(threads, tasks, sizeof(tasks)/sizeof(tasks[0]));
	}
}

#endif /* TASK_PARALLEL_HPP */

