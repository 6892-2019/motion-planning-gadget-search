#include "precompiled.hpp"
#include <future>
#include "bounded_queue.hpp"
#include <gtest/gtest.h>

TEST(BoundedQueueTest, Trivial) {
	bounded_queue<int> queue(5);
	EXPECT_EQ(queue.capacity(), 5);
	queue.put(0);
	queue.put(1);
	queue.put(2);
	queue.put(3);
	queue.put(4);
	EXPECT_EQ(queue.size(), 5);
	EXPECT_EQ(queue.take(), 0);
	EXPECT_EQ(queue.take(), 1);
	EXPECT_EQ(queue.take(), 2);
	EXPECT_EQ(queue.take(), 3);
	EXPECT_EQ(queue.take(), 4);
	EXPECT_EQ(queue.size(), 0);
}

TEST(BoundedQueueTest, TrivialTwoThreads) {
	bounded_queue<int> queue(5);
	EXPECT_EQ(queue.capacity(), 5);
	//these are actually synchronous because the returned future's dtor blocks
	std::async(std::launch::async, [&]{queue.put(0);});
	std::async(std::launch::async, [&]{queue.put(1);});
	std::async(std::launch::async, [&]{queue.put(2);});
	std::async(std::launch::async, [&]{queue.put(3);});
	std::async(std::launch::async, [&]{queue.put(4);});
	EXPECT_EQ(queue.size(), 5);
	EXPECT_EQ(queue.take(), 0);
	EXPECT_EQ(queue.take(), 1);
	EXPECT_EQ(queue.take(), 2);
	EXPECT_EQ(queue.take(), 3);
	EXPECT_EQ(queue.take(), 4);
	EXPECT_EQ(queue.size(), 0);
}

TEST(BoundedQueueTest, SequencedTwoThreads) {
	bounded_queue<int> queue(5);
	EXPECT_EQ(queue.capacity(), 5);
	const auto& future = std::async(std::launch::async, [&]{
		for (int i = 0; i < 5; ++i)
			queue.put(i);
	});
	//can't assert size, but we know we'll get them back in order
	EXPECT_EQ(queue.take(), 0);
	EXPECT_EQ(queue.take(), 1);
	EXPECT_EQ(queue.take(), 2);
	EXPECT_EQ(queue.take(), 3);
	EXPECT_EQ(queue.take(), 4);
	EXPECT_EQ(queue.size(), 0);
	future.wait();
}

TEST(BoundedQueueTest, UsThemSequencedTwoThreads) {
	bounded_queue<int> queue(5);
	EXPECT_EQ(queue.capacity(), 5);
	queue.put(0);
	const auto& future = std::async(std::launch::async, [&]{
		for (int i = 1; i < 5; ++i)
			queue.put(i);
	});
	//we put one in ourselves, so there's at least one in there
	EXPECT_GE(queue.size(), 1);
	EXPECT_EQ(queue.take(), 0);
	EXPECT_EQ(queue.take(), 1);
	EXPECT_EQ(queue.take(), 2);
	EXPECT_EQ(queue.take(), 3);
	EXPECT_EQ(queue.take(), 4);
	EXPECT_EQ(queue.size(), 0);
	future.wait();
}

TEST(BoundedQueueTest, UsThemSequencedLargeTwoThreads) {
	bounded_queue<int> queue(5);
	EXPECT_EQ(queue.capacity(), 5);
	queue.put(0);
	const auto& future = std::async(std::launch::async, [&]{
		for (int i = 1; i < 5000; ++i)
			queue.put(i);
	});
	//we put one in ourselves, so there's at least one in there
	EXPECT_GE(queue.size(), 1);
	for (int i = 0; i < 5000; ++i)
		EXPECT_EQ(queue.take(), i);
	EXPECT_EQ(queue.size(), 0);
	future.wait();
}

TEST(BoundedQueueTest, UsThemUnsequencedLargeManyThreads) {
	bounded_queue<int> queue(5);
	EXPECT_EQ(queue.capacity(), 5);
	queue.put(-1);
	std::vector<std::future<void>> futures;
	for (int i = 0; i < 50; ++i)
		futures.push_back(std::async(std::launch::async, [&queue, i]{
			for (int j = i*100; j < (i+1)*100; ++j)
				queue.put(j);
		}));
	//we put one in ourselves, so there's at least one in there
	EXPECT_GE(queue.size(), 1);
	EXPECT_EQ(queue.take(), -1);
	std::vector<int> results;
	for (int i = 0; i < 5000; ++i)
		results.push_back(queue.take());
	std::sort(results.begin(), results.end());
	EXPECT_EQ(results.size(), 5000);
	for (int i = 0; i < 5000; ++i)
		EXPECT_EQ(results[i], i);
	EXPECT_EQ(queue.size(), 0);
}