#include "precompiled.hpp"
#include "automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
#include "database.hpp"
#include "rpc.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
#include "hopscotch/hopscotch_set.h"
#include "hopscotch/hopscotch_map.h"
#include "tsl/ordered_set.h"
#include "msgpack.hpp"
#include <pqxx/pqxx>
#include <cstdio>

using namespace automaton;
using std::size_t;
using std::pair;
using std::tuple;
using std::optional;
using std::nullopt;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

template<typename T>
void debug_scream([[maybe_unused]] T& t) {
	static_assert(std::is_trivial_v<T>, "must be trivial to scream");
#ifndef NDEBUG
	std::memset(&t, 0xAA, sizeof(T));
#endif //NDEBUG
}

class GadgetBuilder {
public:
	GadgetBuilder(unsigned int alphabet_size, WorkingAutomaton::state_type gadget_state_estimate = 1) : gadget(make_working(alphabet_size)) {
		//Every gadget has at least one state, and it's important that automaton
		//state 0 correspond to a gadget state (to accept the empty string).
		//For other states we can freely intermix the states that correspond to
		//gadget states and those that don't.
		gadget_state_estimate = std::max(gadget_state_estimate, 1u);
		for (WorkingAutomaton::state_type i = 0; i < gadget_state_estimate; ++i)
			translateState(i);
	}
	GadgetBuilder& trans(WorkingAutomaton::state_type start, WorkingAutomaton::symbol_type from,
			WorkingAutomaton::symbol_type to, WorkingAutomaton::state_type end) {
		start = translateState(start);
		end = translateState(end);
		assert(gadget->accept(start));
		assert(gadget->accept(end));
		WorkingAutomaton::state_type t = gadget->addState();
		gadget->addTrans(start, from, t);
		gadget->addTrans(t, to, end);
		return *this;
	}
	unique_ptr<WorkingAutomaton> build() {
		gadget->minimize();
		canonicalize(*gadget, gadget->active_alphabet_size(), false);
		return std::move(gadget);
	}
private:
	unique_ptr<WorkingAutomaton> gadget;
	vector<WorkingAutomaton::state_type> gadgetStateToAutomatonState;

	WorkingAutomaton::state_type translateState(WorkingAutomaton::state_type gadgetState) {
		while (gadgetState >= gadgetStateToAutomatonState.size()) {
			gadgetStateToAutomatonState.push_back(gadget->addState());
			gadget->setAccept(gadgetStateToAutomatonState.back());
		}
		return gadgetStateToAutomatonState[gadgetState];
	}
};

struct GadgetEdge {
	WorkingAutomaton::state_type start;
	WorkingAutomaton::symbol_type from;
	WorkingAutomaton::symbol_type to;
	WorkingAutomaton::state_type end;
	GadgetEdge reverse() const {return {end, to, from, start};}
	MSGPACK_DEFINE_ARRAY(start, from, to, end)
};
bool operator==(const GadgetEdge& l, const GadgetEdge& r) {
	return std::tie(l.start, l.from, l.to, l.end) == std::tie(r.start, r.from, r.to, r.end);
}
bool operator!=(const GadgetEdge& l, const GadgetEdge& r) {
	return !(l == r);
}
bool operator<(const GadgetEdge& l, const GadgetEdge& r) {
	return std::tie(l.start, l.from, l.to, l.end) < std::tie(r.start, r.from, r.to, r.end);
}
namespace std {
template<>
struct hash<GadgetEdge> {
	size_t operator()(const GadgetEdge& e) const {
		//TODO: this is a bad hash function
		size_t x = e.start;
		x = 31*x + e.from;
		x = 31*x + e.to;
		x = 31*x + e.end;
		return x;
	}
};
}


//TODO: these should be appropriately generalized (e.g., not depending on acceptness,
//reviving state-based things) and moved up to the main automaton library.
template<class ForEachDestination, class IsAccept>
auto predecessorless_accept_things(const unsigned int size,
		ForEachDestination&& for_each_destination, IsAccept&& accept) {
	dynarray<unsigned int> predcount(size);
	std::fill(predcount.begin(), predcount.end(), 0u);
	for (auto s : xrange(size))
		for_each_destination(s, [&](unsigned int t){++predcount[t];});

	//We don't want predecessorless nonaccept states to count as predecessors.
	bool progress = true;
	while (progress) {
		progress = false;
		for (unsigned int i = 0; i < size; ++i)
			if (predcount[i] == 0 && !accept(i)) {
				for_each_destination(i, [&](unsigned int t){--predcount[t];});
				//Mark this state as previously considered, not to be repeated.
				predcount[i] = std::numeric_limits<unsigned int>::max();
				progress = true;
			}
	}

	std::vector<unsigned int> retval;
	for (unsigned int i = 0; i < size; ++i)
		if (predcount[i] == 0 && accept(i))
			retval.push_back(i);
	return retval;
}
template<unsigned int N>
auto predecessorless_accept_components(const Automaton<N>& a, const SCCs& sccs) {
	dynarray<unsigned int> state_to_comp(a.state_size());
	for (auto c : xrange(sccs.size()))
		for (auto s : make_range_for_pair(sccs.begin(c), sccs.end(c)))
			state_to_comp[s] = c;
	return predecessorless_accept_things(sccs.size(),
			[&](unsigned int c, auto&& action){
				//We might visit a destination many times if it's targeted by
				//multiple states in the source component, but that's fine as
				//long as we're consistent between increments and decrements.
				for (auto s : make_range_for_pair(sccs.begin(c), sccs.end(c)))
					a.for_each_destination(s, [&](typename Automaton<N>::state_type t) {
						if (state_to_comp[t] != c) //only count edges to other components
							action(state_to_comp[t]);
					});
			},
			[&](unsigned int c) {
				return std::any_of(sccs.begin(c), sccs.end(c), [&](unsigned int s){
					return a.accept(s);
				});
			});
}

auto reachable_accept_components(const AutomatonBase& a, const SCCs& sccs) {
	dynarray<unsigned int> state_to_comp(a.state_size());
	for (auto c : xrange(sccs.size()))
		for (auto s : make_range_for_pair(sccs.begin(c), sccs.end(c)))
			state_to_comp[s] = c;

	std::vector<unsigned int> ret;
	tsl::hopscotch_set<unsigned int> closed;
	circular_deque<unsigned int, 32> worklist;
	closed.insert(0);
	worklist.push_back(0);
	while (!worklist.empty()) {
		unsigned int cur = worklist.pop_front();
		//even if we aren't an accepting component, we can still reach other accepting components
		bool accepting = false;
		for (auto s : make_range_for_pair(sccs.begin(cur), sccs.end(cur))) {
			a.for_each_destination(s, [&](AutomatonBase::state_type t) {
				unsigned int other = state_to_comp[t];
				if (closed.insert(other).second)
					worklist.push_back(other);
			});
			accepting = accepting || a.accept(s);
		}
		if (accepting)
			ret.push_back(cur); //TODO: could template this finishing action, so we just count if we only care about .size()
	}
	return ret;
}


/**
 * A gadget in SLLS format is two lists of undirected and directed GadgetEdges.
 * Undirected edges also represent their reverse edge.
 * (SLLS stands for "state, location, location, state".)
 */
struct SLLS {
	std::vector<GadgetEdge> uedges, dedges;
	MSGPACK_DEFINE_MAP(uedges, dedges)
};

unique_ptr<WorkingAutomaton> inflate_slls(SLLS gadget, unsigned int alphabetSize = 0) {
	unsigned int states = 0;
	if (!alphabetSize) {
		//Size-to-fit by finding the largest used symbol.
		for (auto e : gadget.uedges) {
			alphabetSize = std::max({alphabetSize, e.from, e.to});
			states = std::max({states, e.start, e.end});
		}
		for (auto e : gadget.dedges) {
			alphabetSize = std::max({alphabetSize, e.from, e.to});
			states = std::max({states, e.start, e.end});
		}
		++alphabetSize;
		++states;
	}
	GadgetBuilder b(alphabetSize, states);
	for (auto e : gadget.uedges)
		b.trans(e.start, e.from, e.to, e.end).trans(e.end, e.to, e.from, e.start);
	for (auto e : gadget.dedges)
		b.trans(e.start, e.from, e.to, e.end);
	return b.build();
}

SLLS deflate_slls(const AutomatonBase& a) {
	SLLS gadget;
	auto state_size = a.state_size();
	auto activealpha = a.activeAlphabet();

	unsigned int gadgetStates = 0;
	tsl::hopscotch_map<AutomatonBase::state_type, unsigned int> autoToGadget; //maps automaton states to gadget states
	tsl::hopscotch_set<GadgetEdge> edges;
	for (AutomatonBase::state_type start = 0; start < state_size; ++start) {
		if (!a.accept(start)) continue;
		for (AutomatonBase::symbol_type from : activealpha) {
			auto middle = a.stepDeterministic(start, from);
			if (middle) {
				assert(!a.accept(*middle));
				for (AutomatonBase::symbol_type to : activealpha) {
					auto end = a.stepDeterministic(*middle, to);
					if (end) {
						assert(a.accept(*end));
						//If either is missing, the insert invalidates iterators, so there's not
						//much point in using them.
						if (!autoToGadget.count(start))
							autoToGadget[start] = gadgetStates++;
						if (!autoToGadget.count(*end))
							autoToGadget[*end] = gadgetStates++;
						edges.insert(GadgetEdge{autoToGadget[start], from, to, autoToGadget[*end]});
					}
				}
			}
		}
	}

	MAYBE_UNUSED std::size_t nop_edges = 0;
	for (const GadgetEdge& e : edges) {
		if (!edges.count(e.reverse()))
			gadget.dedges.push_back(e);
		else if (e < e.reverse() || e == e.reverse()) {//only the lesser of the pair; also equal for nop edges
			gadget.uedges.push_back(e);
			if (e == e.reverse())
				++nop_edges;
		}
	}
	//nop edges are undirected, but only appear once in the set
	assert(2*gadget.uedges.size() - nop_edges + gadget.dedges.size() == edges.size());
	return gadget;
}

/**
 * A gadget is uniquely identified in the database as a byte array starting with
 * a count of states, locations, undirected edges and directed edges, each as a
 * varint, followed by the edges.  The undirected edges (if any) precede the
 * directed edges (if any).  Each endpoint is s*locations+l as a varint.  Edges
 * are sorted in natural tuple order before being compressed; undirected edges
 * are encoded as the lesser of the pair of edges they represent.  We
 * additionally store the number of strongly connected components reachable from
 * the initial state and containing at least one accepting state in the row,
 * relevant for gadgets only usable a finite number of times.  We do not store
 * this number in the byte array because it is not needed to interpret the edge
 * list.
 *
 * As the gadget is uniquely described by the byte array, we can deduplicate in
 * the database using only that array (and so only using a single index).  But
 * keeping the counts separate lets us query for the smallest gadgets not yet
 * combined (using a join against the provenance table), so we return them
 * separately for the convenience of the Python code (or maybe eventually to
 * directly insert them in the database).  This redundancy shouldn't cost more
 * than 8 bytes per gadget (at the front of the byte array).
 *
 * When taking gadgets as input, we only need the byte array, as the counts are
 * recoverable from it.
 */
struct OutputRow {
	unsigned int states, locations, uedges, dedges, sccs;
	std::vector<std::byte> edges;
	MSGPACK_DEFINE_ARRAY(states, locations, uedges, dedges, sccs, edges)
};
bool operator==(const OutputRow& a, const OutputRow& b) {
	//The four fields are repeated at the beginning of the byte array, so we
	//could compare them first in a bid to save a pointer dereference.
	return std::equal(a.edges.begin(), a.edges.end(), b.edges.begin(), b.edges.end());
}
bool operator!=(const OutputRow& a, const OutputRow& b) {
	return !(a == b);
}
bool operator<(const OutputRow& a, const OutputRow& b) {
	return std::lexicographical_compare(a.edges.begin(), a.edges.end(), b.edges.begin(), b.edges.end());
}
namespace std {
template<>
struct hash<OutputRow> {
	size_t operator()(const OutputRow& e) const {
		return farmhash::Hash(reinterpret_cast<const char*>(e.edges.data()), e.edges.size());
	}
};
}


// TODO: unify this with pack-detail.hpp's PackReader/Writer
#define VARINT_ONE 244
#define VARINT_TWO 252
#define VARINT_THREE 253
#define VARINT_FOUR 254
#define VARINT_FIVE 255

class PackWriter {
public:
	PackWriter() : data() {}
	PackWriter& write8(unsigned int value) {
		assert((value & 0xFFU) == value);
		writeBytes<1>(value);
		return *this;
	}
	PackWriter& write16(unsigned int value) {
		assert((value & 0xFFFFU) == value);
		writeBytes<2>(value);
		return *this;
	}
	PackWriter& write24(unsigned int value) {
		assert((value & 0xFFFFFFU) == value);
		writeBytes<3>(value);
		return *this;
	}
	PackWriter& write32(unsigned int value) {
		assert((value & 0xFFFFFFFFU) == value);
		writeBytes<4>(value);
		return *this;
	}
	PackWriter& writeBytes(unsigned int value, unsigned int count) {
		switch (count) {
			case 1: return write8(value);
			case 2: return write16(value);
			case 3: return write24(value);
			case 4: return write32(value);
			default:
				assert(false);
				__builtin_unreachable();
		}
	}
	PackWriter& writeVarint(unsigned int value) {
		//inspired by https://sqlite.org/src4/doc/trunk/www/varint.wiki but
		//only with 32-bit range, so recovering a few more small values.
		if (value <= VARINT_ONE)
			write8(value);
		else if (value <= (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE - 1) {
			write8((value - VARINT_ONE) / 256 + VARINT_ONE + 1);
			write8((value - VARINT_ONE) % 256);
		} else if (value <= (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE - 1 + 65536) {
			write8(VARINT_THREE);
			write8((value - ((VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE)) / 256);
			write8((value - ((VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE)) % 256);
		} else if (value <= 16777215) {
			write8(VARINT_FOUR);
			write24(value);
		} else {
			write8(VARINT_FIVE);
			write32(value);
		}
		return *this;
	}

	vector<std::byte> data;
private:
	template<unsigned int N>
	void writeBytes(unsigned int value) {
		std::array<std::byte, N> array;
		std::memcpy(array.begin(), &value, N);
		data.insert(data.end(), array.begin(), array.end());
	}
};

class PackReader {
public:
	PackReader(const vector<std::byte>& data) : data_(data), cur_(0) {}
	unsigned int read8() {
		return readBytes<1>();
	}
	unsigned int read16() {
		return readBytes<2>();
	}
	unsigned int read24() {
		return readBytes<3>();
	}
	unsigned int read32() {
		return readBytes<4>();
	}
	unsigned int readBytes(unsigned int count) {
		switch (count) {
			case 1: return read8();
			case 2: return read16();
			case 3: return read24();
			case 4: return read32();
			default:
				assert(false);
				__builtin_unreachable();
		}
	}
	unsigned int readVarint() {
		unsigned int first = read8();
		if (first <= VARINT_ONE)
			return first;
		if (first <= VARINT_TWO) {
			unsigned int second = read8();
			return VARINT_ONE + 256*(first - VARINT_ONE - 1) + second;
		}
		if (first == VARINT_THREE) {
			unsigned int second = read8();
			unsigned int third = read8();
			return (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE + 256*second + third;
		}
		if (first == VARINT_FOUR)
			return read24();
		if (first == VARINT_FIVE)
			return read32();
		__builtin_unreachable();
	}

	bool eof() const {return cur_ == data_.size();}
private:
	template<unsigned int N>
	unsigned int readBytes() {
		unsigned int value = 0;
		if constexpr (N == 3) {
			//If we won't read off the end by doing so, it's much faster to load
			//an aligned dword and mask off the bytes we want.
			if (cur_ + 4 <= data_.size()) {
				std::memcpy(&value, data_.data()+cur_, 4);
				value &= 0x00FFFFFF;
				cur_ += N;
				return value;
			}
		}

		if (cur_ + N > data_.size())
			throw std::out_of_range("while unpacking");
		std::memcpy(&value, data_.data()+cur_, N);
		cur_ += N;
		return value;
	}
	const vector<std::byte>& data_;
	vector<std::byte>::size_type cur_;
};


unique_ptr<WorkingAutomaton> inflate_outputrow(const std::vector<std::byte>& bytes, unsigned int automaton_size = 0) {
	PackReader data(bytes);
	[[maybe_unused]] unsigned int states = data.readVarint(); //TODO: use this for checking the divisions?
	unsigned int locations = data.readVarint();
	if (automaton_size && locations > automaton_size)
		throw std::logic_error(fmt::format("inflate_outputrow: specified size too small: {} {}", automaton_size, locations));
	GadgetBuilder builder(automaton_size ? automaton_size : locations, states);
	unsigned int undirected_edges = data.readVarint();
	unsigned int directed_edges = data.readVarint();
	//TODO: locations should be less than 16 (for supported inputs), so we
	//could speed up these divmods
	for ([[maybe_unused]] auto _ : xrange(undirected_edges)) {
		unsigned int p = data.readVarint(), q = data.readVarint();
		builder.trans(p / locations, p % locations, q % locations, q / locations);
		builder.trans(q / locations, q % locations, p % locations, p / locations);
	}
	for ([[maybe_unused]] auto _ : xrange(directed_edges)) {
		unsigned int p = data.readVarint(), q = data.readVarint();
		builder.trans(p / locations, p % locations, q % locations, q / locations);
	}
	return builder.build();
}
template<unsigned int N>
unique_ptr<Automaton<N>> inflate_outputrow(const std::vector<std::byte>& bytes) {
	return unique_cast<Automaton<N>>(inflate_outputrow(bytes, N));
}

OutputRow deflate_outputrow(const AutomatonBase& a) {
	SLLS slls = deflate_slls(a);
	unsigned int states = a.accept_size();
	unsigned int locations = a.active_alphabet_size();
	std::sort(slls.uedges.begin(), slls.uedges.end());
	std::sort(slls.dedges.begin(), slls.dedges.end());
	PackWriter data;
	data.writeVarint(states).writeVarint(locations)
			.writeVarint(numeric_cast<unsigned int>(slls.uedges.size()))
			.writeVarint(numeric_cast<unsigned int>(slls.dedges.size()));
	for (GadgetEdge e : slls.uedges)
		data.writeVarint(e.start * locations + e.from).writeVarint(e.end * locations + e.to);
	for (GadgetEdge e : slls.dedges)
		data.writeVarint(e.start * locations + e.from).writeVarint(e.end * locations + e.to);
	SCCs sccs = automaton::find_components(a);
	return {states, locations,
			numeric_cast<unsigned int>(slls.uedges.size()), numeric_cast<unsigned int>(slls.dedges.size()),
			numeric_cast<unsigned int>(reachable_accept_components(a, sccs).size()),
			std::move(data.data)};
}

//TODO: make this SCCs::find
unsigned int component_for_state(SCCs sccs, AutomatonBase::state_type state) {
	for (unsigned int c : xrange(sccs.size()))
		for (unsigned int s : make_range_for_pair(sccs.begin(c), sccs.end(c))) //TODO: add SCCs::range (name TBD)
			if (s == state)
				return c;
	//TODO: add an SCCs method giving the number of states, so we can report here
	throw std::logic_error(fmt::format("component_for_state failed: {} {}", state, sccs.size()));
}

/**
 * Canonicalizes a gadget in SLLS format, returning in database row format.
 * Intended for use when loading human-readable gadget definitions into the
 * database.
 */
vector<pair<OutputRow, optional<OutputRow>>> canonicalize_from_slls(SLLS gadget) {
	unique_ptr<WorkingAutomaton> a = inflate_slls(gadget);
	OutputRow row = deflate_outputrow(*a);
	SCCs sccs = automaton::find_components(*a); //just computed this in deflate_outputrow, could try to save it
	auto activealpha = a->active_alphabet_size();

	vector<pair<unique_ptr<WorkingAutomaton>, OutputRow>> normals;
	normals.emplace_back(std::move(a), std::move(row));
	//When initializing the database with named gadgets, we want to try all
	//initial states in the initial connected component.
	unsigned int initial_component = component_for_state(sccs, 0);
	for (auto state : make_range_for_pair(sccs.begin(initial_component), sccs.end(initial_component))) //TODO: SCCs::range
		if (normals.front().first->accept(state)) {
			unique_ptr<WorkingAutomaton> p = normals.front().first->clone();
			p->swapStateNumbers(0, state);
			canonicalize(*p, activealpha, false); //no mirroring
			row = deflate_outputrow(*p);
			normals.emplace_back(std::move(p), std::move(row));
		}
	std::sort(normals.begin(), normals.end(), [](const auto& l, const auto& r) {return l.second < r.second;});
	normals.erase(std::unique(normals.begin(), normals.end(),
			[](const auto& l, const auto& r) {return l.second == r.second;}), normals.end());

	//It's plausible that only a subset of the states are chiral.
	vector<pair<unique_ptr<WorkingAutomaton>, OutputRow>> mirrors;
	for (const auto& n : normals) {
		//We don't return the rotation, but we won't add a mirror provenance edge
		//either, so the usual mirror machinery will fill it in later.  We just
		//need the gadget up front so we can give it an appropriate name.
		unique_ptr<WorkingAutomaton> p = mirror(*n.first).first;
		row = deflate_outputrow(*p);
		mirrors.emplace_back(std::move(p), std::move(row));
	}

	//Mirror order is the same as the normal order (mirrors aren't sorted).
	//We're also just taking the first enantiomorph as 'normal', rather than
	//the lexicographically lesser one.
	vector<pair<OutputRow, optional<OutputRow>>> retval;
	for (auto i : xrange(normals.size()))
		if (normals[i].second != mirrors[i].second)
			retval.emplace_back(std::move(normals[i].second), std::move(mirrors[i].second));
		else
			retval.emplace_back(std::move(normals[i].second), nullopt);
	return retval;
}


template<class Provenance>
struct Finisher {
	//I'm assuming we aren't generating so many rows as to need the PageHolder
	//machinery to reduce fragmentation.
	tsl::ordered_set<OutputRow> rows_;
	vector<Provenance> prov_;
	std::size_t pruned_ = 0;
	bool operator()(WorkingAutomaton&& a, Provenance prov) {
		//TODO: calling active_alphabet_size again here is wasteful, should pass it in instead
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(canonicalize(a, a.active_alphabet_size(), false));
		return (*this)(deflate_outputrow(a), prov);
	}
	bool operator()(OutputRow&& r, Provenance prov) {
		//caller is responsible for setting canonicalizePermutation
		auto pair = rows_.insert(r);
		if (!pair.second) ++pruned_;
		//When we successfully insert, we know the index is size()-1, but deque
		//operator- is cheap enough that it's not worth branching on .second.
		prov.output1 = numeric_cast<decltype(prov.output1)>(std::distance(rows_.begin(), pair.first));
		prov_.push_back(std::move(prov));
		return pair.second;
	}
};


template<unsigned int N>
bool enjoin(Automaton<N>& a, typename Automaton<N>::symbol_type l, typename Automaton<N>::symbol_type m) {
	using state_type = typename Automaton<N>::state_type;
	bool progress, changed = false;
	//TODO: instead of fixpoint iteration, we should put the changed state s
	//on a worklist and iterate until it's empty
	//TODO: check if we actually need to iterate in the first place -- we shouldn't be adding new edges on l/m...
	do {
		progress = false;
		for (state_type s = 0; s < a.state_size(); ++s) {
			if (a.accept(s)) continue;
			auto dests = a.step(s, l);
			for (state_type d : dests) {
				assert(a.accept(d));
				for (state_type e : a.step(d, m))
					progress |= a.addEpsilon(s, e);
			}

			dests = a.step(s, m);
			for (state_type d : dests) {
				assert(a.accept(d));
				for (state_type e : a.step(d, l))
					progress |= a.addEpsilon(s, e);
			}
		}
		changed |= progress;
	} while (progress);
	return changed;
}

template<unsigned int N>
auto connect_alphamap(unsigned int locations, unsigned int connectPoint) {
	//TODO: these alphamap manipulations could all be precomputed, though it's
	//not clear that would be any faster than using a stack variable
	std::array<unsigned int, Automaton<N>::alphabet_size_v> alphamap;
	if (connectPoint+1 == locations) {
		auto end = alphamap.begin()+locations-2;
		//other connect point is zero, so start from 1
		std::iota(alphamap.begin(), end, 1);
		std::fill(end, alphamap.end(), std::numeric_limits<typename Automaton<N>::symbol_type>::max());
	} else {
		auto middle = alphamap.begin()+connectPoint, end = alphamap.begin()+locations-2;
		std::iota(alphamap.begin(), middle, 0);
		std::iota(middle, end, connectPoint+2);
		std::fill(end, alphamap.end(), std::numeric_limits<typename Automaton<N>::symbol_type>::max());
	}
	return alphamap;
}

template<unsigned int N, class Provenance>
void connect_at(const Automaton<N>& a, unsigned int activeAlphabetSize,
		Provenance prov, Finisher<Provenance>& finisher) {
	Automaton<N> connected = a;
	enjoin(connected, prov.connectPoint, (prov.connectPoint+1) % activeAlphabetSize);
	//We no longer close here.
	auto alphamap = connect_alphamap<N>(activeAlphabetSize, prov.connectPoint);
	connected.renumberAlphabet(alphamap.begin());

	connected.minimize();
	auto active = connected.activeAlphabet();
	if (active.size() <= 1) return; //there are no interesting 1-symbol automata
	//TODO: if this check usually doesn't fire, we can use active_alphabet_size instead of activeAlphabet
	if (active.size() != (activeAlphabetSize - 2)) {
		//compress the alphabet
		active.sort();
		std::array<typename Automaton<N>::symbol_type, Automaton<N>::alphabet_size_v> compression;
		std::copy(active.begin(), active.end(), compression.begin());
		std::fill(compression.begin()+active.size(), compression.end(), std::numeric_limits<typename Automaton<N>::symbol_type>::max());
		connected.renumberAlphabet(compression.begin());
		//Because we're deleting unused symbols, we don't need to
		//minimize again; any two equivalent states would differ only in
		//the symbols we deleted, but those symbols were inactive.
		//TODO: improve Automaton to notice this, or add a renumberAlphabet variant,
		//so that we actually skip minimizing in the finisher's canonicalize.
	}

	finisher(std::move(connected), prov);
}

template<unsigned int N>
void connect(const Automaton<N>& a, std::uint64_t input1, Finisher<ConnectProvenance>& finisher) {
	auto activeAlphabetSize = a.active_alphabet_size();
	//If there are fewer than 4 locations, there will be fewer than 2 surviving
	//after the connect (we'll always delete two locations), so no results.
	if (activeAlphabetSize < 4) return;
	ConnectProvenance prov;
	debug_scream(prov);
	prov.input1 = input1;
	for (auto connectPoint : xrange(activeAlphabetSize)) {
		prov.connectPoint = numeric_cast<std::uint8_t>(connectPoint);
		connect_at(a, activeAlphabetSize, prov, finisher);
	}
}

void connect(const AutomatonBase& a, std::uint64_t input1, Finisher<ConnectProvenance>& finisher) {
	auto alpha = a.alphabet_size();
	if (alpha < 4) {
		//We should ignore these at the database level.
		fmt::print(stderr, "ignoring connect for gadget {} with alphabet size {} (no results possible)\n",
				input1, alpha);
		return;
	}
	switch (alpha) {
#define TOGGLESRUNNER_CONNECT_CASE(N) case N: return connect(static_cast<const Automaton<N>&>(a), input1, finisher);
		TOGGLESRUNNER_CONNECT_CASE(4)
		TOGGLESRUNNER_CONNECT_CASE(5)
		TOGGLESRUNNER_CONNECT_CASE(6)
		TOGGLESRUNNER_CONNECT_CASE(7)
		TOGGLESRUNNER_CONNECT_CASE(8)
		TOGGLESRUNNER_CONNECT_CASE(9)
		TOGGLESRUNNER_CONNECT_CASE(10)
		TOGGLESRUNNER_CONNECT_CASE(11)
		TOGGLESRUNNER_CONNECT_CASE(12)
		TOGGLESRUNNER_CONNECT_CASE(13)
		TOGGLESRUNNER_CONNECT_CASE(14)
		TOGGLESRUNNER_CONNECT_CASE(15)
		TOGGLESRUNNER_CONNECT_CASE(16)
#undef TOGGLESRUNNER_CONNECT_CASE
		default:
			fmt::print(stderr, "unhandled toggles-runner connect for gadget {} with alphabet size {} and typeid {}\n",
					input1, alpha, typeid(a).name());
	}
}

Finisher<ConnectProvenance> do_connect(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<ConnectProvenance> finisher;
	for (auto& i : inputs)
		connect(*inflate_outputrow(i.second), i.first, finisher);
	return finisher;
}

struct ConnectCommandOutput {
//	decltype(Finisher<ConnectProvenance>::rows_.values_container()) rows;
	vector<OutputRow> rows;
	vector<ConnectProvenance> prov;
	//if we ever return toughies, we need Python-side changes, as the other unary commands don't
	MSGPACK_DEFINE_ARRAY(rows, prov)
};
ConnectCommandOutput do_connect_for_python(vector<pair<std::uint64_t, vector<std::byte>>> inputs) { //TODO: ensure we're moving, not copying the arg
	Finisher<ConnectProvenance> finisher = do_connect(std::move(inputs));
	//TODO: Ideally we'd just put values_container (a deque) in the ConnectCommandOutput,
	//but msgpack only provides a packer, and MSGPACK_DEFINE_ARRAY also demands
	//a packer (I guess -- we shouldn't be using it).  Though we end up copying
	//it either way because we can't move it -- maybe tsl::ordered_set needs a release() method?
	std::vector<OutputRow> rows(finisher.rows_.values_container().begin(), finisher.rows_.values_container().end());
	return {std::move(rows), std::move(finisher.prov_)};
}


using RotationVec = boost::container::small_vector<unsigned int, 16>;
template<unsigned int N>
RotationVec find_useful_rotations(const Automaton<N>& a) {
	using symbol_type = AutomatonBase::symbol_type;
	RotationVec useful;
	auto locations = a.active_alphabet_size();
	if (!locations) return useful;

	vector<pair<std::uint64_t, Automaton<N>>> distinct;
	std::array<symbol_type, Automaton<N>::alphabet_size_v> rotation;
	std::iota(rotation.begin(), rotation.end(), 0);
	for (unsigned int rl = 0; rl < locations; ++rl) {
		//It's arbitrary which way we rotate so long as we match what combine does.
		std::iota(rotation.begin(), rotation.begin()+locations, 0);
		std::rotate(rotation.begin(), rotation.begin()+rl, rotation.begin()+locations);
		Automaton<N> rm = a;
		rm.permuteAlphabet(rotation.data());
		rm.canonicalize(); //The normal, non-alphabet-adjusting canonicalize.
		auto our_hash = rm.working_hash();
		bool labeled_continue = false;
		for (const auto& p : distinct)
			if (our_hash == p.first && rm == p.second)
				labeled_continue = true;
		if (labeled_continue) continue;

		distinct.emplace_back(our_hash, std::move(rm));
		useful.push_back(rl);
	}
	return useful;
}

RotationVec find_useful_rotations(const WorkingAutomaton& a) {
	switch (a.alphabet_size()) {
#define TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(N) case N: return find_useful_rotations(static_cast<const Automaton<N>&>(a));
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(1)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(2)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(3)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(4)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(5)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(6)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(7)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(8)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(9)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(10)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(11)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(12)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(13)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(14)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(15)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(16)
#undef TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE
		default:
			fmt::print(stderr, "unhandled find_useful_rotations for alphabet size {}, typeid {}\n",
					a.alphabet_size(), typeid(a).name());
			std::terminate();
	}
}

template<unsigned int Precision>
void combine(const Automaton<Precision>& la, AutomatonBase::state_type leftLocations,
		const Automaton<Precision>& ra, AutomatonBase::state_type rightLocations,
		const RotationVec& rightRotations, CombineProvenance prov, Finisher<CombineProvenance>& finish) {
	using symbol_type = WorkingAutomaton::symbol_type;
	std::array<symbol_type, Automaton<Precision>::alphabet_size_v> slide;
	Automaton<Precision> shiftedRight = ra;
	std::iota(slide.begin(), slide.end(), 0);
	std::rotate(slide.rbegin(), slide.rbegin()+leftLocations, slide.rend());
	shiftedRight.renumberAlphabet(slide.data());
	Automaton<Precision> shuffled = automaton::shuffleAccept(la, shiftedRight);
	shuffled.minimize();
	//Now [0,leftLocations) are from the left and [leftLocations,leftLocations+rightLocations)
	//are from the right.  We want to start inserting at left location 0, so we
	//write all the right locations, then all the left locations.  We'll rotate
	//the right locations as appropriate.  Then we'll move a left location to
	//the other end of the array.  Locations beyond leftLocations+rightLocations
	//are left alone, as they are always inactive.
	std::iota(slide.begin(), slide.begin()+rightLocations, leftLocations);
	std::iota(slide.begin()+rightLocations, slide.begin()+rightLocations+leftLocations, 0);
	std::iota(slide.begin()+rightLocations+leftLocations, slide.end(), rightLocations+leftLocations);
	for (decltype(leftLocations) ll = 0; ll < leftLocations; ++ll) {
		for (auto rotation : rightRotations) {
			//Because we're reading a list of rotations (not rotation deltas),
			//we have to re-initialize the right locations each time.
			std::iota(slide.begin()+ll, slide.begin()+ll+rightLocations, leftLocations);
			std::rotate(slide.begin()+ll, slide.begin()+ll+rotation, slide.begin()+ll+rightLocations);
			Automaton<Precision> permuted = shuffled;
			permuted.permuteAlphabet(slide.data());
			prov.splice = numeric_cast<std::uint8_t>(ll);
			prov.rotation = numeric_cast<std::uint8_t>(rotation);
			prov.connectPoint = numeric_cast<std::uint8_t>(
					(leftLocations+rightLocations+ll-1) % (leftLocations + rightLocations));
			connect_at(permuted, leftLocations + rightLocations, prov, finish);
			prov.connectPoint = numeric_cast<std::uint8_t>(
					(leftLocations+rightLocations+ll+rightLocations-1) % (leftLocations + rightLocations));
			connect_at(permuted, leftLocations + rightLocations, prov, finish);
		}
		std::swap(slide[ll], slide[ll+rightLocations]);
	}
}

template<unsigned int Precision>
Finisher<CombineProvenance> do_combine0(const tsl::hopscotch_map<std::uint64_t, vector<std::byte>>& map,
		const vector<std::uint64_t>& left_gids, const vector<std::uint64_t>& right_gids) {
	vector<unique_ptr<Automaton<Precision>>> right_autos;
	vector<unsigned int> right_locations;
	vector<RotationVec> right_rotations;
	for (std::uint64_t r : right_gids) {
		auto it = map.find(r);
		if (it == map.end())
			throw std::logic_error(fmt::format("right gid {} not in map", r));
		right_autos.push_back(inflate_outputrow<Precision>(it->second));
		right_locations.push_back(right_autos.back()->active_alphabet_size());
		right_rotations.push_back(find_useful_rotations(*right_autos.back()));
	}

	CombineProvenance prov;
	Finisher<CombineProvenance> finisher;
	for (std::uint64_t l : left_gids) {
		prov.input1 = l;
		auto it = map.find(l);
		if (it == map.end())
			throw std::logic_error(fmt::format("left gid {} not in map", l));
		unique_ptr<Automaton<Precision>> pla = inflate_outputrow<Precision>(it->second);
		auto leftLocations = pla->active_alphabet_size();
		for (auto ri : xrange(right_gids.size())) {
			if (leftLocations + right_locations[ri] > Precision) {
				fmt::print(stderr, "WARNING: skipping combine between {} ({} locations) and {} ({} locations) which exceeds precision {}\n",
						l, leftLocations, right_gids[ri], right_locations[ri], Precision);
				continue;
			}
			prov.input2 = right_gids[ri];
			combine(*pla, leftLocations, *right_autos[ri], right_locations[ri], right_rotations[ri], prov, finisher);
		}
	}
	return finisher;
}

Finisher<CombineProvenance> do_combine(tsl::hopscotch_map<std::uint64_t, vector<std::byte>> map,
		const vector<std::uint64_t>& left_gids, const vector<std::uint64_t>& right_gids, unsigned int precision) {
	switch (precision) {
#define TOGGLESRUNNER_DO_COMBINE_CASE(N) case N: return do_combine0<N>(map, left_gids, right_gids);
		TOGGLESRUNNER_DO_COMBINE_CASE(4)
		TOGGLESRUNNER_DO_COMBINE_CASE(5)
		TOGGLESRUNNER_DO_COMBINE_CASE(6)
		TOGGLESRUNNER_DO_COMBINE_CASE(7)
		TOGGLESRUNNER_DO_COMBINE_CASE(8)
		TOGGLESRUNNER_DO_COMBINE_CASE(9)
		TOGGLESRUNNER_DO_COMBINE_CASE(10)
		TOGGLESRUNNER_DO_COMBINE_CASE(11)
		TOGGLESRUNNER_DO_COMBINE_CASE(12)
		TOGGLESRUNNER_DO_COMBINE_CASE(13)
		TOGGLESRUNNER_DO_COMBINE_CASE(14)
		TOGGLESRUNNER_DO_COMBINE_CASE(15)
		TOGGLESRUNNER_DO_COMBINE_CASE(16)
#undef TOGGLESRUNNER_DO_COMBINE_CASE
		case 1:
		case 2:
		case 3:
			fmt::print(stderr, "impossibly small precision for do_combine: {}\n", precision);
			std::terminate();
		default:
			fmt::print(stderr, "unhandled do_combine for precision {}\n", precision);
			std::terminate();
	}
}

struct CombineCommandInput {
	vector<pair<std::uint64_t, vector<std::byte>>> inputs;
	vector<std::uint64_t> lefts, rights;
	unsigned int precision;
	MSGPACK_DEFINE_ARRAY(inputs, lefts, rights, precision)
};
//This could possibly be templatized with ConnectCommandOutput, though we will
//want toughies to be pairs of ids.  The get-stuff-from-Finisher logic can be
//shared with closure and possibly mirror (depending on how Finisher sets
//canonicalize_permutation).
//Maybe toughies is a partially-completed provenance?
struct CombineCommandOutput {
//	decltype(Finisher<ConnectProvenance>::rows_.values_container()) rows;
	vector<OutputRow> rows;
	vector<CombineProvenance> prov;
	vector<pair<std::uint64_t, std::uint64_t>> toughies;
	MSGPACK_DEFINE_ARRAY(rows, prov, toughies)
};
CombineCommandOutput do_combine_for_python(CombineCommandInput cmd) {
	tsl::hopscotch_map<std::uint64_t, vector<std::byte>> map;
	for (auto& p : cmd.inputs)
		map[p.first] = std::move(p.second);
	cmd.inputs.clear();
	Finisher<CombineProvenance> finisher = do_combine(std::move(map), cmd.lefts, cmd.rights, cmd.precision);
	vector<pair<std::uint64_t, std::uint64_t>> toughies;
	//TODO: Ideally we'd just put values_container (a deque) in the ConnectCommandOutput,
	//but msgpack only provides a packer, and MSGPACK_DEFINE_ARRAY also demands
	//a packer (I guess -- we shouldn't be using it).  Though we end up copying
	//it either way because we can't move it -- maybe tsl::ordered_set needs a release() method?
	std::vector<OutputRow> rows(finisher.rows_.values_container().begin(), finisher.rows_.values_container().end());
	return {std::move(rows), std::move(finisher.prov_), std::move(toughies)};
}


struct SimpleOutput {
//	decltype(Finisher<SimpleProvenance>::rows_.values_container()) rows;
	vector<OutputRow> rows;
	vector<SimpleProvenance> prov;
	MSGPACK_DEFINE_ARRAY(rows, prov)
};


Finisher<SimpleProvenance> do_close(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher;
	for (const auto& p : inputs) {
		SimpleProvenance prov;
		prov.input1 = p.first;
		unique_ptr<WorkingAutomaton> a = inflate_outputrow(p.second);
		//TODO: inflate_outputrow could just return the value from the byte array...
		auto activealpha = a->active_alphabet_size();
		bool possibly_changed = acceptingClosure(*a, activealpha);
		if (!possibly_changed) continue;
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(canonicalize(*a, activealpha, false));
		OutputRow r = deflate_outputrow(*a);
		if (r.edges != p.second)
			finisher(std::move(r), prov);
		//TODO: should always be more edges (with undirected counting twice) after
		//successful (modified the automaton) closure, never the same or fewer
	}
	return finisher;
}

SimpleOutput do_close_for_python(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher = do_close(std::move(inputs));
	//TODO: avoid this copy
	std::vector<OutputRow> rows(finisher.rows_.values_container().begin(), finisher.rows_.values_container().end());
	return {std::move(rows), std::move(finisher.prov_)};
}


Finisher<SimpleProvenance> do_mirror(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher;
	for (const auto& p : inputs) {
		SimpleProvenance prov;
		prov.input1 = p.first;
		unique_ptr<WorkingAutomaton> a = inflate_outputrow(p.second);
		//mirror copies.  We do need to know if mirroring changed the automaton,
		//but maybe there's a way to do that while reusing *a?
		pair<unique_ptr<WorkingAutomaton>, unsigned int> m = mirror(*a);
		if (*m.first == *a) continue;
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(m.second);
		finisher(deflate_outputrow(*m.first), prov);
	}
	return finisher;
}

SimpleOutput do_mirror_for_python(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher = do_mirror(std::move(inputs));
	//TODO: avoid this copy
	std::vector<OutputRow> rows(finisher.rows_.values_container().begin(), finisher.rows_.values_container().end());
	return {std::move(rows), std::move(finisher.prov_)};
}



std::string build_select_gadget_data_to_id(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::integer, $2::bytea)"); //first one is special to specify types
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${})", 2*i + 1, 2*i + 2));
	return "with input_rows (n, data) as (values\n" +
			join(values, ",\n  ") +
			"\n)\n" +
			"select input_rows.n, gadgets.id from gadgets join input_rows using (data);";
}

std::string build_insert_gadgets_query(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	//first one is special to specify types
	values.push_back("  ($1::integer, $2::integer, $3::integer, $4::integer, $5::integer, $6::integer, $7::bytea)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${}, ${}, ${}, ${}, ${})",
				7*i+1, 7*i+2, 7*i+3, 7*i+4, 7*i+5, 7*i+6, 7*i+7));
	return "with input_rows (n, states, locations, uedges, dedges, components, data) as (values" +
			join(values, ",\n  ") +
			"\n), ins as (\n"
			"  insert into gadgets (states, locations, undirected_edges, directed_edges, components, data)\n"
			"  select states, locations, uedges, dedges, components, data from input_rows\n"
			"  returning gadgets.id, gadgets.data\n"
			")\n"
			"select input_rows.n, ins.id from input_rows join ins using (data);";
}

std::string build_insert_connect_edges_query(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::bigint, $2::bigint, $3::smallint, $4::smallint)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${}, ${})", 4*i + 1, 4*i + 2, 4*i+3, 4*i+4));
	return "insert into connect_edges (input1, output1, connect_location, canonicalize_rotation) values\n" +
			join(values, ",\n  ") + ";";
}

std::string build_insert_combine_edges_query(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::bigint, $2::bigint, $3::bigint, $4::smallint, $5::smallint, $6::smallint, $7::smallint)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${}, ${}, ${}, ${}, ${})",
				7*i+1, 7*i+2, 7*i+3, 7*i+4, 7*i+5, 7*i+6, 7*i+7));
	return "insert into combine_edges (input1, input2, output1, splice, rotation, connect_location, canonicalize_rotation) values\n" +
			join(values, ",\n  ") + ";";
}

std::string build_insert_simple_edges_query(std::size_t rows, std::string_view table_name, std::string_view column_name_list) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::bigint, $2::bigint, $3::smallint)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${})", 3*i + 1, 3*i + 2, 3*i+3));
	return fmt::format("insert into {} ({}) values\n", table_name, column_name_list) +
			join(values, ",\n  ") +
			"\n on conflict do nothing;";
}

std::string build_insert_close_edges_query(std::size_t rows) {
	return build_insert_simple_edges_query(rows, "close_edges", "input1, output1, canonicalize_rotation");
}
std::string build_insert_mirror_edges_query(std::size_t rows) {
	return build_insert_simple_edges_query(rows, "mirror_edges", "a, b, canonicalize_rotation");
}

std::string build_insert_completion_query(std::size_t rows, std::string_view table_name) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  (int8range($1::bigint, $2::bigint))");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(int8range(${}, ${}))", 2*i + 1, 2*i + 2));
	return fmt::format("insert into {} (r) values\n", table_name) +
			join(values, ",\n  ") + ";";
}

//This is basically a workaround for NetBeans' choking on structured bindings.
//If I ever stop using it, this can just be a pair.
struct SelsertGadgetByDataResult {
	vector<std::uint64_t> local_to_global, novel_global_ids;
};
/**
 * Returns the global gadget id of each of the given rows, inserting the row if
 * not already present.  The vector of ids matches the order of the rows.
 */
SelsertGadgetByDataResult selsert_gadget_by_data(pqxx::connection& conn, transaction& trans,
		//using a specific Finisher instantiation here because it's the same for all of them
		decltype(Finisher<ConnectProvenance>::rows_)&& rows_from_finisher) {
	auto& rows = rows_from_finisher.values_container();
	//TODO: decide if we can persistently prepare when pgbouncer starts a new
	//transaction (for this and other prepared statements)
	if (rows.size() >= 100)
		conn.prepare("select_data_100", build_select_gadget_data_to_id(100));

	vector<std::uint64_t> local_to_global(rows.size(), std::numeric_limits<std::uint64_t>::max());
	std::size_t row_index = 0, pending_insert_count = 0;
	//Do batches of 100 with our prepared statement, then make one
	//parameterized (non-prepared) query for the remainder.
	while (rows.size() - row_index >= 100) {
		pqxx::prepare::invocation inv = trans.prepared("select_data_100");
		for (std::size_t max = row_index + 100; row_index < max; ++row_index) {
			const OutputRow& r = rows[row_index];
			inv(row_index)(pqxx::binarystring(r.edges.data(), r.edges.size()));
		}
		pqxx::result already_have = inv.exec();
		for (pqxx::row r : already_have)
			local_to_global[r[0].as<std::size_t>()] = r[1].as<std::size_t>();
		pending_insert_count += 100 - already_have.size();
	}
	if (row_index < rows.size()) {
		std::size_t epilogue_count = rows.size() - row_index;
		pqxx::internal::parameterized_invocation inv = trans.parameterized(build_select_gadget_data_to_id(epilogue_count));
		for (; row_index < rows.size(); ++row_index) {
			const OutputRow& r = rows[row_index];
			inv(row_index)(pqxx::binarystring(r.edges.data(), r.edges.size()));
		}
		pqxx::result already_have = inv.exec();
		for (pqxx::row r : already_have)
			local_to_global[r[0].as<std::size_t>()] = r[1].as<std::size_t>();
		pending_insert_count += epilogue_count - already_have.size();
	}

	if (pending_insert_count >= 100)
		conn.prepare("insert_gadgets_100", build_insert_gadgets_query(100));

	//Scan over local_to_global building up a batch, then fill it in; also
	//record the global ids of the inserted gadgets.
	vector<std::uint64_t> novel_global_ids;
	novel_global_ids.reserve(pending_insert_count);
	row_index = 0;
	while (pending_insert_count >= 100) {
		pqxx::prepare::invocation inv = trans.prepared("insert_gadgets_100");
		std::size_t batched = 0;
		while (batched++ < 100) {
			while (local_to_global[row_index] != std::numeric_limits<std::uint64_t>::max()) ++row_index;
			const OutputRow& r = rows[row_index];
			inv(row_index)(r.states)(r.locations)(r.uedges)(r.dedges)(r.sccs)(pqxx::binarystring(r.edges.data(), r.edges.size()));
			++row_index;
		}
		pqxx::result inserted = inv.exec();
		for (pqxx::row r : inserted) {
			auto gid = r[1].as<std::size_t>();
			local_to_global[r[0].as<std::size_t>()] = gid;
			novel_global_ids.push_back(gid);
		}
		pending_insert_count -= 100;
	}
	if (pending_insert_count) {
		pqxx::internal::parameterized_invocation inv = trans.parameterized(
				build_insert_gadgets_query(pending_insert_count));
		std::size_t batched = 0;
		while (batched++ < pending_insert_count) {
			while (local_to_global[row_index] != std::numeric_limits<std::uint64_t>::max()) ++row_index;
			const OutputRow& r = rows[row_index];
			inv(row_index)(r.states)(r.locations)(r.uedges)(r.dedges)(r.sccs)(pqxx::binarystring(r.edges.data(), r.edges.size()));
			++row_index;
		}
		pqxx::result inserted = inv.exec();
		for (pqxx::row r : inserted) {
			auto gid = r[1].as<std::size_t>();
			local_to_global[r[0].as<std::size_t>()] = gid;
			novel_global_ids.push_back(gid);
		}
		pending_insert_count -= batched;
	}

	//Should have filled in everything now.
	assert(std::find(local_to_global.begin(), local_to_global.end(),
			std::numeric_limits<std::uint64_t>::max()) == local_to_global.end());
	return {std::move(local_to_global), std::move(novel_global_ids)};
}

void insert_completed_ranges(pqxx::connection& conn, transaction& trans,
		vector<pair<std::uint64_t, std::uint64_t>> ranges,
		string_view table) {
	//It's not clear to me that using prepared statements is actually faster here...
	const unsigned int batch_size = 100;
	std::optional<std::string> prepared_statement_name;
	if (ranges.size() > batch_size) {
		prepared_statement_name.emplace(fmt::format("insert_{}_{}", table, batch_size));
		conn.prepare(*prepared_statement_name, build_insert_completion_query(batch_size, table));
	}
	std::size_t completed_index = 0;
	while (ranges.size() - completed_index >= batch_size) {
		pqxx::prepare::invocation inv = trans.prepared(*prepared_statement_name);
		for (std::size_t max = completed_index + batch_size; completed_index < max; ++completed_index)
			inv(ranges[completed_index].first)(ranges[completed_index].second);
		inv.exec();
	}
	if (completed_index < ranges.size()) {
		pqxx::internal::parameterized_invocation inv = trans.parameterized(
				build_insert_completion_query(ranges.size() - completed_index, table));
		for (; completed_index < ranges.size(); ++completed_index)
			inv(ranges[completed_index].first)(ranges[completed_index].second);
		inv.exec();
	}
}

vector<pair<std::uint64_t, std::uint64_t>> maximal_ranges(const vector<std::uint64_t>& data) {
	assert(std::is_sorted(data.begin(), data.end()));
	vector<pair<std::uint64_t, std::uint64_t>> ranges;
	auto first = data.begin(), last = data.begin();
	//Build maximal ranges, first inclusive and last exclusive.
	while (true) {
		if (last+1 == data.end()) {
			ranges.emplace_back(*first, *last + 1);
			break;
		} else if (*(last+1) - *last != 1) {
			ranges.emplace_back(*first, *last + 1);
			first = last = last+1;
		} else
			++last;
	}
	return ranges;
}

static std::string g_database_connect_string;

DatabaseOperationStatistics do_connect_db(vector<std::uint64_t> input_gids) {
	pqxx::connection conn(g_database_connect_string);
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);

	Finisher outputs = do_connect(std::move(inputs));
	std::size_t survivor_size = outputs.rows_.size();

	const unsigned int batch_size = 200;
	if (outputs.prov_.size() >= batch_size)
		conn.prepare("insert_connect_edge_batch", build_insert_connect_edges_query(200));

	std::size_t novel_gadgets_size = retry_db_operation([&](){
		transaction trans(conn);

		auto selsert_result = selsert_gadget_by_data(conn, trans, std::move(outputs.rows_));
		const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;

		std::size_t edge_index = 0;
		while (outputs.prov_.size() - edge_index >= batch_size) {
			pqxx::prepare::invocation inv = trans.prepared("insert_connect_edge_batch");
			for (std::size_t max = edge_index + batch_size; edge_index < max; ++edge_index) {
				const ConnectProvenance& p = outputs.prov_[edge_index];
				inv(p.input1)(local_to_global[p.output1])((unsigned short)p.connectPoint)((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}
		if (edge_index < outputs.prov_.size()) {
			pqxx::internal::parameterized_invocation inv = trans.parameterized(
					build_insert_connect_edges_query(outputs.prov_.size() - edge_index));
			for (; edge_index < outputs.prov_.size(); ++edge_index) {
				const ConnectProvenance& p = outputs.prov_[edge_index];
				inv(p.input1)(local_to_global[p.output1])((unsigned short)p.connectPoint)((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}

		std::sort(input_gids.begin(), input_gids.end());
		insert_completed_ranges(conn, trans, maximal_ranges(input_gids), "completed_connects");

		trans.commit();
		return selsert_result.novel_global_ids.size();
	});
	return {outputs.pruned_, survivor_size - novel_gadgets_size, novel_gadgets_size, outputs.prov_.size()};
}

DatabaseOperationStatistics do_combine_db(vector<std::uint64_t> left_gids, vector<std::uint64_t> right_gids, unsigned int precision) {
	vector<std::uint64_t> input_gids;
	input_gids.reserve(left_gids.size() + right_gids.size());
	input_gids.insert(input_gids.end(), left_gids.begin(), left_gids.end());
	input_gids.insert(input_gids.end(), right_gids.begin(), right_gids.end());
	std::sort(input_gids.begin(), input_gids.end());
	input_gids.erase(std::unique(input_gids.begin(), input_gids.end()), input_gids.end());

	pqxx::connection conn(g_database_connect_string);

	//TODO: select_gadget_id_to_data should be templated on the result container so we can directly build this map
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);
	tsl::hopscotch_map<std::uint64_t, vector<std::byte>> map;
	for (pair<std::uint64_t, vector<std::byte>>& p : inputs)
		map.try_emplace(p.first, std::move(p.second));
	Finisher outputs = do_combine(std::move(map), left_gids, right_gids, precision);
	std::size_t survivor_size = outputs.rows_.size();

	const unsigned int batch_size = 200;
	if (outputs.prov_.size() >= batch_size)
		conn.prepare("insert_combine_edge_batch", build_insert_combine_edges_query(200));

	std::size_t novel_gadgets_size = retry_db_operation([&](){
		transaction trans(conn);

		auto selsert_result = selsert_gadget_by_data(conn, trans, std::move(outputs.rows_));
		const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;

		std::size_t edge_index = 0;
		while (outputs.prov_.size() - edge_index >= batch_size) {
			pqxx::prepare::invocation inv = trans.prepared("insert_combine_edge_batch");
			for (std::size_t max = edge_index + batch_size; edge_index < max; ++edge_index) {
				const CombineProvenance& p = outputs.prov_[edge_index];
				inv(p.input1)(p.input2)(local_to_global[p.output1])
						((unsigned short)p.splice)((unsigned short)p.rotation)
						((unsigned short)p.connectPoint)((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}
		if (edge_index < outputs.prov_.size()) {
			pqxx::internal::parameterized_invocation inv = trans.parameterized(
					build_insert_combine_edges_query(outputs.prov_.size() - edge_index));
			for (; edge_index < outputs.prov_.size(); ++edge_index) {
				const CombineProvenance& p = outputs.prov_[edge_index];
				inv(p.input1)(p.input2)(local_to_global[p.output1])
						((unsigned short)p.splice)((unsigned short)p.rotation)
						((unsigned short)p.connectPoint)((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}

		trans.commit();
		return selsert_result.novel_global_ids.size();
	});
	return {outputs.pruned_, survivor_size - novel_gadgets_size, novel_gadgets_size, outputs.prov_.size()};
}

DatabaseOperationStatistics do_close_db(vector<std::uint64_t> input_gids) {
	pqxx::connection conn(g_database_connect_string);

	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);

	Finisher<SimpleProvenance> outputs = do_close(std::move(inputs));
	std::size_t survivor_size = outputs.rows_.size();

	if (outputs.prov_.size() >= 200)
		conn.prepare("insert_close_edge_200", build_insert_close_edges_query(200));

	std::size_t novel_gadgets_size = retry_db_operation([&](){
		transaction trans(conn);

		//could be structured bindings
		auto selsert_result = selsert_gadget_by_data(conn, trans, std::move(outputs.rows_));
		const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;
		vector<std::uint64_t>& novel_global_ids = selsert_result.novel_global_ids;

		std::size_t edge_index = 0;
		while (outputs.prov_.size() - edge_index >= 200) {
			pqxx::prepare::invocation inv = trans.prepared("insert_close_edge_200");
			for (std::size_t max = edge_index + 200; edge_index < max; ++edge_index) {
				const SimpleProvenance& p = outputs.prov_[edge_index];
				inv(p.input1)(local_to_global[p.output1])((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}
		if (edge_index < outputs.prov_.size()) {
			pqxx::internal::parameterized_invocation inv = trans.parameterized(
					build_insert_close_edges_query(outputs.prov_.size() - edge_index));
			for (; edge_index < outputs.prov_.size(); ++edge_index) {
				const SimpleProvenance& p = outputs.prov_[edge_index];
				inv(p.input1)(local_to_global[p.output1])((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}

		//Closure is idempotent, so we've also finished for any new gadgets.
		input_gids.insert(input_gids.end(), novel_global_ids.begin(), novel_global_ids.end());
		std::sort(input_gids.begin(), input_gids.end());
		insert_completed_ranges(conn, trans, maximal_ranges(input_gids), "completed_closes");

		trans.commit();
		return novel_global_ids.size();
	}, 10);

	return {outputs.pruned_, survivor_size - novel_gadgets_size, novel_gadgets_size, outputs.prov_.size()};
}

DatabaseOperationStatistics do_mirror_db(vector<std::uint64_t> input_gids) {
	pqxx::connection conn(g_database_connect_string);

	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);

	Finisher<SimpleProvenance> outputs = do_mirror(std::move(inputs));
	std::size_t survivor_size = outputs.rows_.size();

	const unsigned int batch_size = 200;
	if (outputs.prov_.size() >= batch_size)
		conn.prepare("insert_mirror_edge_batch", build_insert_mirror_edges_query(batch_size));

	std::size_t novel_gadgets_size = retry_db_operation([&](){
		transaction trans(conn);

		//could be structured bindings
		auto selsert_result = selsert_gadget_by_data(conn, trans, std::move(outputs.rows_));
		const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;
		vector<std::uint64_t>& novel_global_ids = selsert_result.novel_global_ids;

		//We have to sort the edge's vertices after remapping, but we don't need
		//to deduplicate due "on conflict do nothing".
		std::size_t edge_index = 0;
		while (outputs.prov_.size() - edge_index >= batch_size) {
			pqxx::prepare::invocation inv = trans.prepared("insert_mirror_edge_batch");
			for (std::size_t max = edge_index + batch_size; edge_index < max; ++edge_index) {
				const SimpleProvenance& p = outputs.prov_[edge_index];
				std::uint64_t output = local_to_global[p.output1];
				inv(std::min(p.input1, output))(std::max(p.input1, output))((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}
		if (edge_index < outputs.prov_.size()) {
			pqxx::internal::parameterized_invocation inv = trans.parameterized(
					build_insert_mirror_edges_query(outputs.prov_.size() - edge_index));
			for (; edge_index < outputs.prov_.size(); ++edge_index) {
				const SimpleProvenance& p = outputs.prov_[edge_index];
				std::uint64_t output = local_to_global[p.output1];
				inv(std::min(p.input1, output))(std::max(p.input1, output))((unsigned short)p.canonicalizePermutation);
			}
			inv.exec();
		}

		//Mirror is undirected, so we've also finished for any new gadgets.
		input_gids.insert(input_gids.end(), novel_global_ids.begin(), novel_global_ids.end());
		std::sort(input_gids.begin(), input_gids.end());
		insert_completed_ranges(conn, trans, maximal_ranges(input_gids), "completed_mirrors");

		trans.commit();
		return novel_global_ids.size();
	}, 10);

	return {outputs.pruned_, survivor_size - novel_gadgets_size, novel_gadgets_size, outputs.prov_.size()};
}



/**
 * @return a string containing various information about this worker
 */
std::string do_ping() {
	//maybe information about the parent process (might be socat)
	//hostname
	//try to get information about stdin/stdout if they're sockets
	//information about slurm environment variables (if any)
	return "pong";
}



const std::pair<string_view, handler_ptr> handlers[] = {
	{"ping"sv, &handler_adapter<do_ping>},

	{"canonicalize"sv, &handler_adapter<canonicalize_from_slls>},

	{"connect"sv, &handler_adapter<do_connect_for_python>},
	{"combine"sv, &handler_adapter<do_combine_for_python>},
	{"close"sv, &handler_adapter<do_close_for_python>},
	{"mirror"sv, &handler_adapter<do_mirror_for_python>},

	{"connect-db"sv, &handler_adapter<do_connect_db>},
	{"combine-db"sv, &handler_adapter<do_combine_db>},
	{"close-db"sv, &handler_adapter<do_close_db>},
	{"mirror-db"sv, &handler_adapter<do_mirror_db>},
};



vector<char> exhaust_stdin() {
	vector<char> data;
	data.resize(4096, 0);
	size_t index = 0;
	while (true) {
		size_t count = data.size() - index;
		size_t bytes_read = std::fread(&data[index], sizeof(unsigned char), count, stdin);
		if (bytes_read != count) {
			if (std::feof(stdin)) {
				data.resize(index + bytes_read);
				return data;
			}
			if (std::ferror(stdin)) {
				auto savederrno = errno;
				fmt::print(stderr, "error reading from stdin: {} ({}), after reading {} before and {} last\n",
						strerror(savederrno), savederrno, index, bytes_read);
				std::exit(1);
			}
		} else {
			index += bytes_read;
			data.resize(std::min(data.size() * 2, data.size() + 1024*1024*1024), 0);
		}
	}
	//We always return out of the loop or exit(1).
}

msgpack::object_handle read_input() {
	vector<char> input = exhaust_stdin();
	//TODO: optionally decompress the message (based on a command-line option)
	return msgpack::unpack(input.data(), input.size());
}

void write_output(const void* data, size_t size) {
	//TODO: optional compression based on command-line option
	size_t index = 0;
	while (index < size) {
		size_t count = size - index;
		size_t bytes_written = std::fwrite(reinterpret_cast<const char*>(data) + index, sizeof(char), count, stdout);
		if (bytes_written != count) {
			if (std::ferror(stdout)) {
				auto savederrno = errno;
				fmt::print(stderr, "error writing to stdout: {} ({}), after writing {} before and {} last\n",
						strerror(savederrno), savederrno, index, bytes_written);
				std::exit(1);
			} else
				//Unusual enough to be worth remarking about.
				fmt::print(stderr, "Short fwrite? index {}, count {}, wrote {} (short by {})\n",
						index, count, bytes_written, (count - bytes_written));
		}
		index += bytes_written;
	}
	std::fflush(stdout);
}

int main(int argc, char* argv[]) { //genbuild entrypoint
//	MessageFormat input_format = MessageFormat::json, output_format = MessageFormat::json;
//	for (int a = 1; a < argc; ++a) {
//		if (argv[a] == "--input-format=json"sv)
//			input_format = MessageFormat::json;
//		else if (argv[a] == "--input-format=msgpack"sv)
//			input_format = MessageFormat::msgpack;
//		else if (argv[a] == "--output-format=json"sv)
//			output_format = MessageFormat::json;
//		else if (argv[a] == "--output-format=msgpack"sv)
//			output_format = MessageFormat::msgpack;
//	}

	g_database_connect_string = format_connect_string("jbosboom", "", "127.0.0.1", "5432", "togglesearch");

	simple_buffer response = dispatch(read_input(), std::begin(handlers), std::end(handlers));
	write_output(response.data(), response.size());

	return 0;
}