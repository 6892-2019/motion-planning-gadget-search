#ifndef FARMHASH_UTIL_HPP
#define FARMHASH_UTIL_HPP

#include <farmhash/farmhash.h>

namespace farmhash {
	inline std::uint64_t Hash(const std::byte* s, std::size_t len) {
		return farmhash::Hash(reinterpret_cast<const char*>(s), len);
	}
	inline std::uint64_t Hash(const std::vector<std::byte>& x) {
		return farmhash::Hash(x.data(), x.size());
	}
	inline std::uint64_t Fingerprint64(const std::vector<std::byte>& x) {
		return farmhash::Fingerprint64(reinterpret_cast<const char*>(x.data()), x.size());
	}
}

struct farmhash_hash {
	//std::hash<uint64_t> is the identity, and hopscotch doesn't like that.
	std::uint64_t operator()(std::uint64_t x) const noexcept {
		return farmhash::Fingerprint(x);
	}
	std::uint64_t operator()(const std::vector<std::byte>& x) const noexcept {
		return farmhash::Hash(x);
	}
	std::uint64_t operator()(const std::vector<unsigned long>& x) const noexcept {
		return farmhash::Hash(reinterpret_cast<const char*>(x.data()), x.size());
	}
	std::uint64_t operator()(std::string_view x) const noexcept {
		return farmhash::Hash(x.data(), x.size());
	}

	//This should be a template for pair, but we want to use farmhash_hash if we
	//have an overload and std::hash if not.  Maybe that means having
	//operator()(T) as a most-general fallback, and then individual overrides?
	std::uint64_t operator()(const std::pair<unsigned int, unsigned int>& x) const noexcept {
		std::array<unsigned int, 2> data = {x.first, x.second};
		return farmhash::Hash(reinterpret_cast<std::byte*>(data.begin()), data.size() * sizeof(data.front()));
	}
};

#endif /* FARMHASH_UTIL_HPP */

