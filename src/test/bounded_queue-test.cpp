#include "precompiled.hpp"
#include <future>
#include "bounded_queue.hpp"
#include <doctest/doctest.h>

TEST_CASE("BoundedQueueTest_Trivial") {
	bounded_queue<int> queue(5);
	CHECK_EQ(queue.capacity(), 5);
	queue.put(0);
	queue.put(1);
	queue.put(2);
	queue.put(3);
	queue.put(4);
	CHECK_EQ(queue.size(), 5);
	CHECK_EQ(queue.take(), 0);
	CHECK_EQ(queue.take(), 1);
	CHECK_EQ(queue.take(), 2);
	CHECK_EQ(queue.take(), 3);
	CHECK_EQ(queue.take(), 4);
	CHECK_EQ(queue.size(), 0);
}

TEST_CASE("BoundedQueueTest_TrivialTwoThreads") {
	bounded_queue<int> queue(5);
	CHECK_EQ(queue.capacity(), 5);
	//these are actually synchronous because the returned future's dtor blocks
	{ auto f = std::async(std::launch::async, [&]{queue.put(0);}); }
	{ auto f = std::async(std::launch::async, [&]{queue.put(1);}); }
	{ auto f = std::async(std::launch::async, [&]{queue.put(2);}); }
	{ auto f = std::async(std::launch::async, [&]{queue.put(3);}); }
	{ auto f = std::async(std::launch::async, [&]{queue.put(4);}); }
	CHECK_EQ(queue.size(), 5);
	CHECK_EQ(queue.take(), 0);
	CHECK_EQ(queue.take(), 1);
	CHECK_EQ(queue.take(), 2);
	CHECK_EQ(queue.take(), 3);
	CHECK_EQ(queue.take(), 4);
	CHECK_EQ(queue.size(), 0);
}

TEST_CASE("BoundedQueueTest_SequencedTwoThreads") {
	bounded_queue<int> queue(5);
	CHECK_EQ(queue.capacity(), 5);
	const auto& future = std::async(std::launch::async, [&]{
		for (int i = 0; i < 5; ++i)
			queue.put(i);
	});
	//can't assert size, but we know we'll get them back in order
	CHECK_EQ(queue.take(), 0);
	CHECK_EQ(queue.take(), 1);
	CHECK_EQ(queue.take(), 2);
	CHECK_EQ(queue.take(), 3);
	CHECK_EQ(queue.take(), 4);
	CHECK_EQ(queue.size(), 0);
	future.wait();
}

TEST_CASE("BoundedQueueTest_UsThemSequencedTwoThreads") {
	bounded_queue<int> queue(5);
	CHECK_EQ(queue.capacity(), 5);
	queue.put(0);
	const auto& future = std::async(std::launch::async, [&]{
		for (int i = 1; i < 5; ++i)
			queue.put(i);
	});
	//we put one in ourselves, so there's at least one in there
	CHECK_GE(queue.size(), 1);
	CHECK_EQ(queue.take(), 0);
	CHECK_EQ(queue.take(), 1);
	CHECK_EQ(queue.take(), 2);
	CHECK_EQ(queue.take(), 3);
	CHECK_EQ(queue.take(), 4);
	CHECK_EQ(queue.size(), 0);
	future.wait();
}

TEST_CASE("BoundedQueueTest_UsThemSequencedLargeTwoThreads") {
	bounded_queue<int> queue(5);
	CHECK_EQ(queue.capacity(), 5);
	queue.put(0);
	const auto& future = std::async(std::launch::async, [&]{
		for (int i = 1; i < 5000; ++i)
			queue.put(i);
	});
	//we put one in ourselves, so there's at least one in there
	CHECK_GE(queue.size(), 1);
	for (int i = 0; i < 5000; ++i)
		CHECK_EQ(queue.take(), i);
	CHECK_EQ(queue.size(), 0);
	future.wait();
}

TEST_CASE("BoundedQueueTest_UsThemUnsequencedLargeManyThreads") {
	bounded_queue<int> queue(5);
	CHECK_EQ(queue.capacity(), 5);
	queue.put(-1);
	std::vector<std::future<void>> futures;
	for (int i = 0; i < 50; ++i)
		futures.push_back(std::async(std::launch::async, [&queue, i]{
			for (int j = i*100; j < (i+1)*100; ++j)
				queue.put(j);
		}));
	//we put one in ourselves, so there's at least one in there
	CHECK_GE(queue.size(), 1);
	CHECK_EQ(queue.take(), -1);
	std::vector<int> results;
	for (int i = 0; i < 5000; ++i)
		results.push_back(queue.take());
	std::sort(results.begin(), results.end());
	CHECK_EQ(results.size(), 5000);
	for (int i = 0; i < 5000; ++i)
		CHECK_EQ(results[i], i);
	CHECK_EQ(queue.size(), 0);
}

TEST_CASE("BoundedQueueTest_MoveOnly") {
	bounded_queue<std::unique_ptr<int>> queue(5);
	queue.put(std::make_unique<int>(0));
	queue.offer(std::make_unique<int>(1));
	auto p = queue.take();
	auto q = queue.poll().value();
	CHECK_EQ(*p, 0);
	CHECK_EQ(*q, 1);
}