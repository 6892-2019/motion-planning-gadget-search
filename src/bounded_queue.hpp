// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
#ifndef BOUNDED_QUEUE_HPP
#define BOUNDED_QUEUE_HPP

#include <mutex>
#include <condition_variable>
#include <boost/circular_buffer.hpp>

/**
 * A threadsafe, bounded, optionally-blocking queue.
 */
template<class T>
class bounded_queue {
public:
	using size_type = unsigned int;
	bounded_queue(size_type size) : queue_(size) {}

	size_type size() const {
		lock_guard lock(mutex_);
		return static_cast<size_type>(queue_.size());
	}
	size_type capacity() const {
		//no lock necessary
		return static_cast<size_type>(queue_.capacity());
	}

	void put(const T& item) {
		wait_guard lock(mutex_);
		nonfull_.wait(lock, [&]{return !queue_.full();});
		queue_.push_back(item);
		nonempty_.notify_one();
	}
	void put(T&& item) {
		wait_guard lock(mutex_);
		nonfull_.wait(lock, [&]{return !queue_.full();});
		queue_.push_back(std::move(item));
		nonempty_.notify_one();
	}
	bool offer(const T& item) {
		lock_guard lock(mutex_);
		if (queue_.full()) return false;
		queue_.push_back(item);
		nonempty_.notify_one();
		return true;
	}
	bool offer(T&& item) {
		lock_guard lock(mutex_);
		if (queue_.full()) return false;
		queue_.push_back(std::move(item));
		nonempty_.notify_one();
		return true;
	}

	T take() {
		wait_guard lock(mutex_);
		nonempty_.wait(lock, [&]{return !queue_.empty();});
		T ret = std::move(queue_.front());
		queue_.pop_front();
		nonfull_.notify_one();
		return ret;
	}
	std::optional<T> poll() {
		lock_guard lock(mutex_);
		if (queue_.empty()) return std::nullopt;
		std::optional<T> ret = std::move(queue_.front());
		queue_.pop_front();
		nonfull_.notify_one();
		return ret;
	}
private:
	using mutex_type = std::mutex;
	using lock_guard = std::lock_guard<mutex_type>;
	using wait_guard = std::unique_lock<mutex_type>;
	mutable std::mutex mutex_;
	std::condition_variable nonempty_, nonfull_;
	boost::circular_buffer<T> queue_;
};

#endif /* BOUNDED_QUEUE_HPP */

