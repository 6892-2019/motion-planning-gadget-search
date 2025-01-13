// SPDX-License-Identifier: MIT
// Copyright 2025 Jeffrey Bosboom
#ifndef HASHUTILS_HPP
#define HASHUTILS_HPP

#include <iterator>
#define XXH_INLINE_ALL
#include <xxhash.h>

template<typename T>
std::size_t hash_contiguous_range(const T* data, std::size_t count) noexcept {
	static_assert(std::has_unique_object_representations_v<T>);
	return XXH3_64bits(data, count * sizeof(*data));
}
inline std::size_t hash_contiguous_range(std::nullptr_t data, std::size_t count) noexcept {
	assert(count == 0);
	return XXH3_64bits(nullptr, 0);
}

struct object_hash {
	// std::hash for integral types is usually the identity function.
	// hopscotch_map/set perform poorly with identity hashing.
	template<typename T>
	auto operator()(const T& t) const noexcept {
		return hash_contiguous_range(std::addressof(t), 1);
	}
};

struct contig_range_hash {
	template<typename T>
	auto operator()(const T& t) const noexcept {
		using std::data, std::size;
		return hash_contiguous_range(data(t), size(t));
	}
};

#endif /* HASHUTILS_HPP */
