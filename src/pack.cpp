#include "precompiled.hpp"
#include "pack.hpp"
#include "pack-detail.hpp"

namespace automaton {

Pack* pack(AutomatonBase& a, Pack* first, Pack* last) {
	return nullptr;
}

Pack* unpack(WorkingAutomaton& a, const Pack* first, const Pack* last) {
	return nullptr;
}

unsigned int packed_size(const Pack* pack) {
	return 0;
}
std::size_t packed_hash(const Pack* pack) {
	return 0;
}
bool packed_equal(const Pack* left, const Pack* right) {
	return false;
}

} //namespace automaton