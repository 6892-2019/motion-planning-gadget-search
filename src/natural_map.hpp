/*
 * File:   natural_map.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on November 8, 2016, 12:59 AM
 */

#ifndef NATURAL_MAP_HPP
#define NATURAL_MAP_HPP

template<class K, typename Enable = void>
struct MinimalPerfectHash {
	//TODO: can we static_assert here for a better error message?
};

template<class K>
struct MinimalPerfectHash<K, std::enable_if_t<std::is_unsigned<K>::value>> {
	explicit MinimalPerfectHash(K bound) {}
	std::size_t compress(K key) const {
		return key;
	}
	K reconstitute(std::size_t offset) const {
		return static_cast<K>(offset);
	}
};

template<class K>
struct MinimalPerfectHash<std::pair<K, K>, std::enable_if_t<std::is_unsigned<K>::value>> {
	using value_type = std::pair<K, K>;
	const value_type bound_;
	MinimalPerfectHash(value_type bound) : bound_(bound) {}
	std::size_t compress(value_type key) const {
		return key.first * bound_.second + key.second;
	}
	value_type reconstitute(std::size_t offset) const {
		return {offset / bound_.second, offset % bound_.second};
	}
};

/**
 * A map from natural numbers in a runtime-specified range 0..n-1 to data of an
 * unsigned integer type.
 *
 * TODO: inserting/assigning absent() manually will cause the key to disappear,
 * except that size() isn't updated...
 */
template<class K, class V>
class natural_map {
public:
	using value_type = std::pair<K, V>;
	explicit natural_map(K bound) : hash_(bound), data_(new V[hash_.compress(bound)]), size_(0), capacity_(hash_.compress(bound)) {
		std::fill(data_.get(), data_.get()+capacity_, absent());
	}

	class iterator {
	public:
		value_type operator*() const {
			assert(map_.data_[pos_] != absent()); //check iterator validity
			return {map_.hash_.reconstitute(pos_), map_.data_[pos_]};
		}
		//TODO: We commonly access map iterators as it->first/second to access
		//the key/value, but operator-> must return a pointer and we don't have
		//a pair<K, V> to point to.  We could store one in the iterator
		//temporarily, for well-behaved K types...
		iterator& operator++() {
			assert(pos_ < map_.capacity_ && "incrementing end()");
			++pos_;
			while (pos_ < map_.capacity_ && map_.data_[pos_] == absent())
				++pos_;
			return *this;
		}
		friend bool operator==(const iterator& left, const iterator& right) {
			//can only compare iterators from the same map
			assert(std::addressof(left.map_) == std::addressof(right.map_));
			return left.pos_ == right.pos_;
		}
		friend bool operator!=(const iterator& left, const iterator& right) {
			return !(left == right);
		}
	private:
		iterator(const natural_map& map, std::size_t pos) : map_(map), pos_(pos) {}
		const natural_map& map_;
		std::size_t pos_;
		friend natural_map;
	};

	bool empty() {
		return size() == 0;
	}
	std::size_t size() {
		return size_;
	}

	iterator begin() const {
		//There's no find_not.
		V* first = std::find_if(data_.get(), data_.get()+capacity_, [](const V& v){return v != absent();});
		return {*this, static_cast<std::size_t>(first-data_.get())};
	}
	iterator end() const {
		return {*this, static_cast<std::size_t>(capacity_)};
	}

	std::size_t count(K key) const {
		std::size_t pos = hash_.compress(key);
		//Technically we could say that keys >= capacity are never in the set,
		//but asking is probably an error.
		assert(pos < capacity_);
		return data_[pos] != absent() ? 1 : 0;
	}
	iterator find(K key) const {
		return count(key) ? iterator{*this, hash_.compress(key)} : end();
	}

	V& operator[](K key) {
		std::size_t pos = hash_.compress(key);
		assert(pos < capacity_);
		if (data_[pos] == absent())
			insert({key, static_cast<V>(0)});
		return data_[pos];
	}
	const V& operator[](K key) const {
		std::size_t pos = hash_.compress(key);
		assert(pos < capacity_);
		assert(data_[pos] != absent());
		return data_[pos];
	}

	std::pair<iterator, bool> insert(value_type p) {
		std::size_t pos = hash_.compress(p.first);
		assert(pos < capacity_);
		bool inserted = data_[pos] == absent();
		data_[pos] = p.second;
		if (inserted)
			++size_;
		return {{*this, pos}, inserted};
	}
private:
	MinimalPerfectHash<K> hash_;
	std::unique_ptr<V[]> data_;
	std::size_t size_;
	std::size_t capacity_;
	static_assert(std::is_unsigned<V>::value, "only unsigned types");

	//This is cleaner than a variable because we don't have to define it in an
	//object file.  (C++17 will have inline variables.)
	constexpr static V absent() {
		return std::numeric_limits<V>::max();
	}
};

#endif /* NATURAL_MAP_HPP */

