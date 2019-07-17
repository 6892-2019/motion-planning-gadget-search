#ifndef TRANSFORM_REDUCE_HPP
#define TRANSFORM_REDUCE_HPP

#include <vector>
#include <atomic>
#include <future>

namespace detail {
template<typename Result, class Reducer>
Result transform_reduce_parallel_merge(Reducer reducer, typename std::vector<Result>::iterator first,
		typename std::vector<Result>::iterator last, unsigned int threads) {
	std::size_t size = std::distance(first, last);
	if (size == 1)
		return std::move(*first);
	if (size == 2)
		return reducer(std::move(*first), std::move(*(first+1)));

	auto midpoint = first + size/2;
	if (threads > 1) {
		unsigned int right_threads = threads/2;
		std::future<Result> right = std::async(std::launch::async, [=](){
			return transform_reduce_parallel_merge<Result>(reducer, midpoint, last, right_threads);
		});
		Result left = transform_reduce_parallel_merge<Result>(reducer, first, midpoint, threads - right_threads);
		return reducer(std::move(left), right.get());
	} else {
		Result left = transform_reduce_parallel_merge<Result>(reducer, first, midpoint, 1),
				right = transform_reduce_parallel_merge<Result>(reducer, midpoint, last, 1);
		return reducer(std::move(left), std::move(right));
	}
}
} //namespace detail

template<typename Task, class Transformer, class Reducer>
auto transform_reduce(std::vector<Task>&& tasks, unsigned int threads,
		Transformer&& transformer, Reducer&& reducer) {
	using Result = decltype(transformer(tasks[0]));
	std::vector<Result> results_to_merge(tasks.size());
	std::atomic<std::size_t> task_index_dispenser(0);

	std::vector<std::future<void>> futures;
	for (std::size_t i = 0; i < threads && i < tasks.size(); ++i)
		futures.push_back(std::async(std::launch::async, [&]() {
			Transformer local_transformer = transformer;
			for (std::size_t index = task_index_dispenser++; index < tasks.size(); index = task_index_dispenser++)
				results_to_merge[index] = local_transformer(std::move(tasks[index]));
		}));
	for (std::size_t i = 0; i < futures.size(); ++i)
		futures[i].wait();

	return detail::transform_reduce_parallel_merge<Result>(std::forward<Reducer>(reducer),
			results_to_merge.begin(), results_to_merge.end(), threads);
}

#endif /* TRANSFORM_REDUCE_HPP */

