#ifndef TRANSFORM_REDUCE_HPP
#define TRANSFORM_REDUCE_HPP

#include "dynarray.hpp"
#include <vector>
#include <atomic>
#include <future>

template<typename Task, class Transformer, class Reducer>
auto transform_reduce(std::vector<Task>&& tasks, unsigned int threads,
		Transformer&& transformer, Reducer&& reducer) {
	using Result = decltype(transformer(tasks[0]));
	if (tasks.empty())
		return Result{};
	if (tasks.size() == 1)
		return std::forward<Transformer>(transformer)(std::move(tasks[0]));

	//results[i] is effectively local to the thread executing task i, until it
	//publishes a pointer to it in pubs.
	std::vector<Result> results(tasks.size());
	//An implicit binary tree (internal nodes only).  Each task publishes a
	//pointer to results[i] in its parent node, or if the other child already
	//did, does the reduce and stores in that parent, and so on.  The last
	//result is stored back into results[0].
	dynarray<std::atomic<Result*>> pubs(tasks.size()-1); //tasks.size() == 0/1 already handled above
	for (auto& i : pubs) //why is initializing std::atomic so hard?
		std::atomic_init(&i, nullptr);
	std::atomic<std::size_t> task_index_dispenser(0);

	std::vector<std::future<void>> futures;
	for (std::size_t i = 0; i < threads && i < tasks.size(); ++i)
		futures.push_back(std::async(std::launch::async, [&]() {
			Transformer local_transformer = transformer;
			Reducer local_reducer = reducer;
			for (std::size_t index = task_index_dispenser++; index < tasks.size(); index = task_index_dispenser++) {
				results[index] = local_transformer(std::move(tasks[index]));

				std::size_t tree_node = index + pubs.size();
				do {
					std::size_t parent = (tree_node-1)/2, sibling = tree_node + (tree_node & 0x1 ? 1 : -1);
					Result* old = nullptr;
					if (pubs[parent].compare_exchange_strong(old, &results[index], std::memory_order::acq_rel))
						break; //stored our result; other child will continue reduction chain

					if (tree_node < sibling)
						results[index] = local_reducer(std::move(results[index]), std::move(*old));
					else
						results[index] = local_reducer(std::move(*old), std::move(results[index]));
					//No task threads will read pubs[parent] again, but the
					//caller thread will read pubs[0] to find the final result.
					//Synchronization is through waiting on the future, so we
					//can use relaxed ordering.
					pubs[parent].store(&results[index], std::memory_order::relaxed);
					tree_node = parent;
				} while (tree_node);
			}
		}));
	for (std::size_t i = 0; i < futures.size(); ++i)
		futures[i].wait();

	return std::move(*pubs[0].load(std::memory_order::relaxed));
}

#endif /* TRANSFORM_REDUCE_HPP */

