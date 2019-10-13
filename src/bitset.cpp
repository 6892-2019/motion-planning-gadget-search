#include "precompiled.hpp"
#include "bitset.hpp"

#define BITSET_EXTERN_TEMPLATE /* not extern */
#include "bitset-instantiations.hpp"

namespace automaton {
namespace impl {

[[noreturn, gnu::cold]] void bitset_throw_out_of_range(unsigned int index, unsigned int size) {
	std::string msg = "bitset index out of range: index " + std::to_string(index) + ", size " + std::to_string(size);
	throw std::out_of_range(msg);
}

}//namespace impl
}//namespace automaton