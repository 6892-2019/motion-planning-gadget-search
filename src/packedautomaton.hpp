/*
 * File:   packedautomaton.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 17, 2017, 6:09 PM
 */

#ifndef PACKEDAUTOMATON_HPP
#define PACKEDAUTOMATON_HPP

#include "automatonbase.hpp"

namespace automaton {

/**
 * Overrides non-const AutomatonBase methods to print a message and terminate().
 * This class is technically unnecessary if everything else is correct (we'll
 * never have a non-const reference to an ImmutableAutomaton subclass), but it's
 * convenient to only override these once.
 */
class ImmutableAutomaton : public AutomatonBase {
private:
	[[noreturn]] void die(const char* whence) {
		std::cout << whence << std::endl;
		std::terminate();
		__builtin_unreachable();
	}
public:
	bool addEpsilon(state_type from, state_type to) override final {
		die(__PRETTY_FUNCTION__);
	}
	AutomatonBase::state_type addState() override final {
		die(__PRETTY_FUNCTION__);
	}
	bool addTrans(state_type from, symbol_type on, state_type to) override final {
		die(__PRETTY_FUNCTION__);
	}
	bool addTrans(state_type from, SymbolSet on, state_type to) override final {
		die(__PRETTY_FUNCTION__);
	}
	void clear() override final {
		die(__PRETTY_FUNCTION__);
	}
	void reserve(state_type state_capacity) override final {
		die(__PRETTY_FUNCTION__);
	}
	bool setAccept(state_type state, bool accepts) override final {
		die(__PRETTY_FUNCTION__);
	}
};

/**
 * PackedAutomaton is an immutable automaton that can be hashed and compared for
 * equality through a (typically co-allocated) byte string.  Packed automata are
 * created deterministically via the automaton::pack free function, ensuring
 * equal automata are consistently of the same PackedAutomaton derived class.
 */
class PackedAutomaton : public ImmutableAutomaton {
	//unfortunately this needs to be in the header so clients know it derives
	//from AutomatonBase.
	virtual const unsigned char* storage_begin() const = 0;
	virtual const unsigned char* storage_end() const = 0;
	friend struct std::hash<PackedAutomaton>;
	friend bool operator==(const PackedAutomaton& left, const PackedAutomaton& right);
public:
	bool deterministic() const override final;
	bool minimal() const override final;
	bool canonical() const override final;
	std::size_t packed_hash() const;
	// automaton::pack allocates variable-sized storage for PackedAutomaton
	// subclasses.  We need to explicitly declare a non-sized operator delete
	// to prevent the default (sized) delete from doing the wrong thing.
	// see http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0722r1.html
	static void operator delete(void* ptr) {
		::operator delete(ptr);
	}
};
inline bool operator!=(const PackedAutomaton& left, const PackedAutomaton& right) {
	return !(left == right);
}

} //namespace automaton

namespace std {
template<>
struct hash<automaton::PackedAutomaton> {
	std::size_t operator()(const automaton::PackedAutomaton& a) const {
		return a.packed_hash();
	}
};
}

namespace automaton {
std::unique_ptr<const PackedAutomaton> pack(const AutomatonBase& a);
} //namespace automaton



#endif /* PACKEDAUTOMATON_HPP */

