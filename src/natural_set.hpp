/*
 * File:   natural_set.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on November 5, 2016, 10:07 PM
 */

#ifndef NATURAL_SET_HPP
#define NATURAL_SET_HPP

#include "precompiled.hpp"

/**
 * A set of natural numbers in a runtime-specified range 0..n-1.
 *
 * This is not an STL container.  Besides not supporting the full interface, our
 * iterators return values instead of references, because we aren't storing the
 * element type.  (We could try returning a reference to an iterator member
 * variable, but then all the 'elements' would share an address, which is
 * surprising and detectable.)
 */
template<class element_type>
class natural_set {
public:
	using value_type = element_type;
	natural_set(value_type count) : data_(new bool[count]), size_(0), capacity_(count) {
		assert(count >= 0);
		std::fill(data_.get(), data_.get()+capacity_, false);
	}

	class iterator {
	public:
		value_type operator*() const {
			assert(set_.data_[pos_]); //check iterator validity
			return pos_;
		}
		iterator& operator++() {
			assert(pos_ < set_.capacity_ && "incrementing end()");
			++pos_;
			while (pos_ < set_.capacity_ && !set_.data_[pos_])
				++pos_;
			return *this;
		}
		friend bool operator==(const iterator& left, const iterator& right) {
			//can only compare iterators from the same set
			assert(std::addressof(left.set_) == std::addressof(right.set_));
			return left.pos_ == right.pos_;
		}
		friend bool operator!=(const iterator& left, const iterator& right) {
			return !(left == right);
		}
	private:
		iterator(const natural_set& set, value_type pos) : set_(set), pos_(pos) {}
		const natural_set& set_;
		value_type pos_;
		friend natural_set;
	};

	bool empty() {
		return size() == 0;
	}
	std::size_t size() {
		return size_;
	}

	iterator begin() const {
		bool* first = std::find(data_.get(), data_.get()+capacity_, true);
		return {*this, static_cast<value_type>(first-data_.get())};
	}
	iterator end() const {
		return {*this, static_cast<value_type>(capacity_)};
	}

	std::size_t count(value_type k) const {
		//Technically we could say that keys >= capacity are never in the set,
		//but asking is probably an error.
		assert(k < capacity_);
		return data_[k] ? 1 : 0;
	}
	iterator find(value_type k) const {
		return count(k) ? iterator{*this, k} : end();
	}

	std::pair<iterator, bool> insert(value_type k) {
		assert(k < capacity_);
		bool inserted = !data_[k];
		data_[k] = true;
		if (inserted)
			++size_;
		return {{*this, k}, inserted};
	}
private:
	std::unique_ptr<bool[]> data_;
	std::size_t size_;
	std::size_t capacity_;
	static_assert(std::is_unsigned<element_type>::value, "only unsigned types");
};

extern template class natural_set<unsigned char>;
extern template class natural_set<unsigned short>;
extern template class natural_set<unsigned int>;
extern template class natural_set<unsigned long>;
extern template class natural_set<unsigned long long>;

#endif /* NATURAL_SET_HPP */

