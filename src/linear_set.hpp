/*
 * File:   linear_set.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on April 11, 2017, 9:47 PM
 */

#ifndef LINEAR_SET_HPP
#define LINEAR_SET_HPP

#include <algorithm>
#include <utility>
#include <boost/container/small_vector.hpp>

/**
 * A set implementation backed by a vector-like sequence container.  Containment
 * is O(n), but reasonable for small sets.  To preserve the set invariant, only
 * const iterators are provided.  Iteration is in arbitrary order, except that
 * iteration after insertion without removals is in first-insertion order and
 * sort() is provided to enable sorted iteration (std::sort requires non-const
 * iterators, of course).  Mutation may invalidate iterators as for a vector;
 * operator[] and at() are provided for indexed access.
 */
template<typename T>
class linear_set {
private:
	//TODO: use more elements for small types and vice-versa (aim for a
	//particular sizeof(linear_set))
	boost::container::small_vector<T, 4> data_;
public:
	using key_type = T;
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = value_type&;
	using const_reference = const value_type&;
	using const_iterator = typename decltype(data_)::const_iterator;
	using iterator = const_iterator;

	linear_set() = default;

	bool empty() const {
		return data_.empty();
	}
	size_type size() const {
		return data_.size();
	}
	//max_size()? don't know what to return

	iterator begin() {
		return cbegin();
	}
	const_iterator begin() const {
		return cbegin();
	}
	const_iterator cbegin() const {
		return data_.cbegin();
	}
	iterator end() {
		return cend();
	}
	const_iterator end() const {
		return cend();
	}
	const_iterator cend() const {
		return data_.cend();
	}

	const_reference front() {
		return data_.front();
	}
	const_reference front() const {
		return data_.front();
	}
	const_reference back() {
		return data_.back();
	}
	const_reference back() const {
		return data_.back();
	}

	bool count(const key_type& key) const {
		return find(key) != end();
	}
	//find isn't any better than std::find here; this is just to match set's interface
	const_iterator find(const value_type& key) const {
		return std::find(data_.begin(), data_.end(), key);
	}

	void reserve(size_type size) {
		data_.reserve(size);
	}
	void clear() {
		data_.clear();
	}
	std::pair<iterator, bool> insert(const value_type& value) {
		auto i = find(value);
		if (i != end())
			return {i, false};
		data_.push_back(value);
		return {end()-1, true};
	}
	//TODO: rvalue overload, range overloads for insert
	//TODO: emplace
	iterator erase(const_iterator pos) {
		auto idx = pos - begin();
		std::iter_swap(data_.begin()+idx, data_.end()-1);
		data_.pop_back();
		return begin() + idx;
	}
	bool erase(const key_type& key) {
		//if (auto i = find(key); i != end())
		auto i = find(key);
		if (i != end()) {
			erase(key);
			return true;
		}
		return false;
	}
	void swap(linear_set& other) {
		using std::swap;
		swap(data_, other.data_);
	}

	const_reference operator[](size_type index) {
		return data_[index];
	}
	const_reference at(size_type index) {
		return data_.at(index);
	}

	template<class Compare = std::less<key_type>>
	void sort(Compare cmp = Compare()) {
		std::sort(data_.begin(), data_.end(), cmp);
	}
};

template<typename T>
bool operator==(const linear_set<T>& left, const linear_set<T>& right) {
	if (left.size() != right.size()) return false;
	if (left.size() < right.size())
		return right == left;
	for (const T& l : left)
		if (!right.count(l))
			return false;
	return true;
}
template<typename T>
bool operator!=(const linear_set<T>& left, const linear_set<T>& right) {
	return !(left == right);
}

namespace std {
template<typename T>
void swap(linear_set<T>& left, linear_set<T>& right) {
	left.swap(right);
}

template<typename T>
struct hash<linear_set<T>> {
	size_t operator()(const linear_set<T>& s) const {
		size_t h = 0;
		for (const auto& t : s)
			h += std::hash<T>()(t);
		return h;
	}
};
}

#endif /* LINEAR_SET_HPP */

