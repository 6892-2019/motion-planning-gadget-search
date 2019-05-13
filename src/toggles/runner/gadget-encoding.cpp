#include "precompiled.hpp"
#include "gadget-encoding.hpp"
#include "canonicalize.hpp"
#include "pack-detail.hpp"
#include <boost/container/static_vector.hpp>

using namespace automaton;
using automaton::detail::PackReader;
using automaton::detail::PackWriter;
using encoding::GadgetEdge;
using std::unique_ptr;
using std::vector;

//TODO: put this in top-level util file somewhere
//std::hash<uint64_t> is the identity, and hopscotch doesn't like that.
struct farmhash_hash {
	uint64_t operator()(uint64_t x) const noexcept {
		return farmhash::Fingerprint(x);
	}
};

namespace {
//TODO: put this in numutils.hpp?
unsigned int minimum_size(unsigned int x) {
	if (x <= std::numeric_limits<std::uint8_t>::max())
		return 1;
	if (x <= std::numeric_limits<std::uint16_t>::max())
		return 2;
	if (x < (1u << 24))
		return 3;
//	if (x <= std::numeric_limits<std::uint32_t>::max())
	return 4;
}

auto reachable_accept_components(const AutomatonBase& a, const SCCs& sccs) {
	dynarray<unsigned int> state_to_comp(a.state_size());
	for (auto c : xrange(sccs.size()))
		for (auto s : make_range_for_pair(sccs.begin(c), sccs.end(c)))
			state_to_comp[s] = c;

	std::vector<unsigned int> ret;
	tsl::hopscotch_set<unsigned int, farmhash_hash> closed;
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

std::pair<std::vector<GadgetEdge>, std::vector<GadgetEdge>> deflate_slls(const AutomatonBase& a) {
	assert(a.canonical());
	auto state_size = a.state_size();
	auto activealpha = a.activeAlphabet();

	unsigned int gadgetStates = 0;
	tsl::hopscotch_map<AutomatonBase::state_type, unsigned int, farmhash_hash> autoToGadget; //maps automaton states to gadget states
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
	std::vector<GadgetEdge> uedges, dedges;
	for (const GadgetEdge& e : edges) {
		if (!edges.count(e.reverse()))
			dedges.push_back(e);
		else if (e < e.reverse() || e == e.reverse()) {//only the lesser of the pair; also equal for nop edges
			uedges.push_back(e);
			if (e == e.reverse())
				++nop_edges;
		}
	}
	//nop edges are undirected, but only appear once in the set
	assert(2*uedges.size() - nop_edges + dedges.size() == edges.size());
	return {std::move(uedges), std::move(dedges)};
}



struct LLSSEdgeCoder {
	unsigned int locations, states;
	LLSSEdgeCoder(unsigned int location, unsigned int state) : locations(location), states(state) {}
	std::uint64_t encode(const GadgetEdge& e) const {
		std::uint64_t i = ((std::uint64_t)e.from * locations * states * states) +
				(e.to * states * states) +
				(e.start * states) +
				e.end;
		assert(i < max_value());
		return i;
	}
	GadgetEdge decode(std::uint64_t i) const {
		assert(i < max_value());
		//TODO: libdivide divmod optimization
		GadgetEdge e;
		e.from = numeric_cast<unsigned int>(i / (locations * states * states));
		i %= locations * states * states;
		e.to = numeric_cast<unsigned int>(i / (states * states));
		i %= states * states;
		e.start = numeric_cast<unsigned int>(i / states);
		i %= states;
		e.end = numeric_cast<unsigned int>(i);
		return e;
	}
	std::uint64_t max_value() const {
		return locations * locations * states * states;
	}
	//TODO: this may not inline well even if we're passing a constant pointer
	static bool edge_ordering(const GadgetEdge& a, const GadgetEdge& b) {
		return std::tie(a.from, a.to, a.start, a.end) < std::tie(b.from, b.to, b.start, b.end);
	}
};

template<class EdgeCoder>
std::size_t compressed_size(const std::vector<GadgetEdge>& edges, const EdgeCoder& coder) {
	std::size_t size = 0;
	std::uint64_t previous = 0;
	for (const GadgetEdge& e : edges) {
		std::uint64_t cur = coder.encode(e);
		size += PackWriter::varint_size(numeric_cast<unsigned int>(cur - previous));
		previous = cur;
	}
	return size;
}
template<class EdgeCoder>
std::size_t write_compressed(const std::vector<GadgetEdge>& edges, const EdgeCoder& coder, PackWriter& writer) {
	std::size_t size = 0;
	std::uint64_t previous = 0;
	for (const GadgetEdge& e : edges) {
		std::uint64_t cur = coder.encode(e);
		writer.writeVarint(numeric_cast<unsigned int>(cur - previous));
		previous = cur;
	}
	return size;
}

template<bool directed, class EdgeCoder>
void read_compressed(PackReader& reader, unsigned int count, const EdgeCoder& coder, GadgetBuilder& builder) {
	std::uint64_t previous = 0;
	for (unsigned int i = 0; i < count; ++i) {
		std::uint64_t cur = reader.readVarint();
		cur += previous;
		GadgetEdge e = coder.decode(cur);
		builder.trans(e.start, e.from, e.to, e.end);
		if (!directed)
			builder.trans(e.end, e.to, e.from, e.start);
		previous = cur;
	}
}
} //anonymous namespace



namespace encoding {

std::unique_ptr<WorkingAutomaton> inflate_slls(const std::vector<GadgetEdge>& uedges,
		const std::vector<GadgetEdge>& dedges, unsigned int alphabetSize) {
	unsigned int states = 0;
	if (!alphabetSize) {
		//Size-to-fit by finding the largest used symbol.
		for (auto e : uedges) {
			alphabetSize = std::max({alphabetSize, e.from, e.to});
			states = std::max({states, e.start, e.end});
		}
		for (auto e : dedges) {
			alphabetSize = std::max({alphabetSize, e.from, e.to});
			states = std::max({states, e.start, e.end});
		}
		++alphabetSize;
		++states;
	}
	GadgetBuilder b(alphabetSize, states);
	for (auto e : uedges)
		b.trans(e.start, e.from, e.to, e.end).trans(e.end, e.to, e.from, e.start);
	for (auto e : dedges)
		b.trans(e.start, e.from, e.to, e.end);
	return b.build();
}



/**
 * The packed gadget encoding has a two-byte header, a few words of statistics
 * information, and variable-length edge-list data.
 *
 * The low four bits of the first byte store the number of locations - 1 (we
 * don't care to store 0, and we do care to store 16).  The remaining four bits
 * are reserved.
 *
 * The second byte stores the lengths of further statistics information in its
 * six low bits.  The top two bits are reserved.
 *   bit 6-7: reserved
 *   bit 5: 1 or 2 bytes holding the number of states
 *   bit 3-4: 0, 1, 2 or 3 bytes holding the number of undirected edges
 *   bit 1-2: 0, 1, 2 or 3 bytes holding the number of directed edges
 *   bit 0: 0 or 1 bytes holding the number of strongly-connected components
 * When 0 bytes are allocated for edges, the corresponding count is 0; when 0
 * bytes are allocated for components, the count is 1.
 */

namespace detail {
constexpr static std::byte location_mask{0b1111};

constexpr static std::byte state_mask    {0b00100000};
constexpr static std::byte uedge_mask    {0b00011000};
constexpr static std::byte dedge_mask    {0b00000110};
constexpr static std::byte component_mask{0b00000001};

using edge_coder = LLSSEdgeCoder;

std::pair<Stats, const std::byte*> stats(const std::byte* encoded_gadget) {
	Stats s = {};
	s.locations = locations(encoded_gadget++);

	std::byte second_byte = *encoded_gadget++;
	std::size_t state_length = 1 + static_cast<bool>(second_byte & detail::state_mask);
	std::size_t uedge_length = std::to_integer<unsigned int>(second_byte & detail::uedge_mask) >> 3;
	std::size_t dedge_length = std::to_integer<unsigned int>(second_byte & detail::dedge_mask) >> 1;
	std::size_t comp_length = std::to_integer<unsigned int>(second_byte & detail::component_mask);

	std::memcpy(&s.states, encoded_gadget, state_length);
	std::memcpy(&s.undirected_edges, encoded_gadget + state_length, uedge_length);
	std::memcpy(&s.directed_edges, encoded_gadget + state_length + uedge_length, dedge_length);
	std::memcpy(&s.components, encoded_gadget + state_length + uedge_length + dedge_length, comp_length);
	encoded_gadget += state_length + uedge_length + dedge_length + comp_length;

	return {s, encoded_gadget};
}

auto build_header(Stats s) {
	boost::container::static_vector<std::byte, 11> header;
	unsigned int state_length = s.states == 0 ? 0 : minimum_size(s.states);
	unsigned int uedge_length = s.undirected_edges == 0 ? 0 : minimum_size(s.undirected_edges);
	unsigned int dedge_length = s.directed_edges == 0 ? 0 : minimum_size(s.directed_edges);
	unsigned int comp_length = s.components == 1 ? 0 : minimum_size(s.components);

	//TODO: mark these all unlikely
	if (state_length > 2)
		throw std::overflow_error(fmt::format("too many states {}", s));
	if (uedge_length > 3)
		throw std::overflow_error(fmt::format("too many uedges {}", s));
	if (dedge_length > 3)
		throw std::overflow_error(fmt::format("too many dedges {}", s));
	if (comp_length > 1)
		throw std::overflow_error(fmt::format("too many components {}", s));
	if (s.locations == 0)
		throw std::logic_error(fmt::format("gadget with no locations? {}", s));
	if (s.locations > 16)
		throw std::logic_error(fmt::format("gadget with too many locations? {}", s));
	if (state_length == 0)
		throw std::logic_error(fmt::format("gadget with no states? {}", s));
	if (uedge_length == 0 && dedge_length == 0)
		throw std::logic_error(fmt::format("gadget with no edges? {}", s));

	header.resize(2 + state_length + uedge_length + dedge_length + comp_length);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
	header[0] = std::byte{s.locations - 1};
	header[1] = (std::byte{state_length - 1} << 5) | (std::byte{uedge_length} << 3) |
			(std::byte{dedge_length} << 1) | std::byte{comp_length};
#pragma GCC diagnostic pop
	std::byte* p = &header[2];
	std::memcpy(p, &s.states, state_length);
	std::memcpy(p + state_length, &s.undirected_edges, uedge_length);
	std::memcpy(p + state_length + uedge_length, &s.directed_edges, dedge_length);
	std::memcpy(p + state_length + uedge_length + dedge_length, &s.components, comp_length);
	return header;
}
} //namespace detail

unsigned int locations(const std::byte* encoded_gadget) {
	return std::to_integer<unsigned int>(*encoded_gadget & detail::location_mask) + 1;
}

Stats stats(const std::byte* encoded_gadget) {
	return detail::stats(encoded_gadget).first;
}

std::vector<std::byte> encode(const automaton::WorkingAutomaton& a) {
	assert(a.canonical());
	auto&& [uedges, dedges] = deflate_slls(a);
	Stats stats = {};
	stats.locations = a.active_alphabet_size();
	stats.states = a.accept_size();
	stats.undirected_edges = numeric_cast<unsigned int>(uedges.size());
	stats.directed_edges = numeric_cast<unsigned int>(dedges.size());
	stats.components = numeric_cast<unsigned int>(
			reachable_accept_components(a, automaton::find_components(a)).size());
	auto header = detail::build_header(stats);

	detail::edge_coder coder(stats.locations, stats.states);
	std::sort(uedges.begin(), uedges.end(), detail::edge_coder::edge_ordering);
	std::sort(dedges.begin(), dedges.end(), detail::edge_coder::edge_ordering);
	std::size_t uedge_compsize = compressed_size(uedges, coder),
			dedge_compsize = compressed_size(dedges, coder);

	std::vector<std::byte> data(header.size() + uedge_compsize + dedge_compsize);
	std::copy(header.begin(), header.end(), data.begin());
	PackWriter writer(data.data() + header.size(), data.data() + data.size());
	write_compressed(uedges, coder, writer);
	write_compressed(dedges, coder, writer);
	assert(writer.tell() == data.data() + data.size());
	assert(!writer.overflow());
	return data;
}

std::unique_ptr<automaton::WorkingAutomaton> decode(const std::byte* encoded_gadget, std::size_t length,
		unsigned int alphabet_size) {
	auto&& [stats, edgelist_begin] = detail::stats(encoded_gadget);
	//TODO: unlikely
	if (alphabet_size && stats.locations > alphabet_size)
		throw std::logic_error(fmt::format("decode: specified size too small: {} {}", alphabet_size, stats));

	GadgetBuilder builder(alphabet_size ? alphabet_size : stats.locations);
	//The coder always uses the actual locations because that's what we encoded with.
	detail::edge_coder coder(stats.locations, stats.states);
	//This length calculation (and the function's length parameter) is just for
	//error checking.  We know how many edges to read, just not how many bytes
	//they were encoded with.
	PackReader reader(edgelist_begin, encoded_gadget + length);
	read_compressed<false>(reader, stats.undirected_edges, coder, builder);
	read_compressed<true>(reader, stats.directed_edges, coder, builder);
	assert(reader.tell() == encoded_gadget + length);
	assert(!reader.overflow());
	return builder.build();
}

} //namespace encoding