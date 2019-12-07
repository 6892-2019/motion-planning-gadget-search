#include "precompiled.hpp"
#include "gadget-encoding.hpp"
#include "canonicalize.hpp"
#include "pack-detail.hpp"
#include <boost/container/static_vector.hpp>

#ifndef NDEBUG
static constexpr bool assertions_enabled = true;
#else
static constexpr bool assertions_enabled = false;
#endif

using namespace automaton;
using automaton::detail::PackReader;
using automaton::detail::PackWriter;
using encoding::GadgetEdge;
using encoding::GadgetBuilder;
using std::unique_ptr;
using std::vector;

namespace {
//renumberAlphabet can't be virtualized because it's templated on the iterator type, argh.
void renumberAlphabet(WorkingAutomaton& p, unsigned int* renumbering) {
	switch (p.alphabet_size()) {
		case  2: static_cast<Automaton< 2>&>(p).renumberAlphabet(renumbering); break;
		case  3: static_cast<Automaton< 3>&>(p).renumberAlphabet(renumbering); break;
		case  4: static_cast<Automaton< 4>&>(p).renumberAlphabet(renumbering); break;
		case  5: static_cast<Automaton< 5>&>(p).renumberAlphabet(renumbering); break;
		case  6: static_cast<Automaton< 6>&>(p).renumberAlphabet(renumbering); break;
		case  7: static_cast<Automaton< 7>&>(p).renumberAlphabet(renumbering); break;
		case  8: static_cast<Automaton< 8>&>(p).renumberAlphabet(renumbering); break;
		case  9: static_cast<Automaton< 9>&>(p).renumberAlphabet(renumbering); break;
		case 10: static_cast<Automaton<10>&>(p).renumberAlphabet(renumbering); break;
		case 11: static_cast<Automaton<11>&>(p).renumberAlphabet(renumbering); break;
		case 12: static_cast<Automaton<12>&>(p).renumberAlphabet(renumbering); break;
		case 13: static_cast<Automaton<13>&>(p).renumberAlphabet(renumbering); break;
		case 14: static_cast<Automaton<14>&>(p).renumberAlphabet(renumbering); break;
		case 15: static_cast<Automaton<15>&>(p).renumberAlphabet(renumbering); break;
		case 16: static_cast<Automaton<16>&>(p).renumberAlphabet(renumbering); break;
		default:
			throw std::logic_error(fmt::format("bad renumberAlphabet size {}", p.alphabet_size()));
	}
}
void compress_alphabet(WorkingAutomaton& p) {
	//Compare similar code in ops.cpp's connect_at.
	auto active = p.activeAlphabet();
	active.sort();
	if (!active.empty() && active.back() != active.size()-1) {
		if (p.alphabet_size() > 16)
			throw std::logic_error(fmt::format("GadgetBuilder::build too big {}", p.alphabet_size()));
		std::array<WorkingAutomaton::symbol_type, 16> compression;
		std::copy(active.begin(), active.end(), compression.begin());
		std::fill(compression.begin()+active.size(), compression.end(), std::numeric_limits<WorkingAutomaton::symbol_type>::max());
		renumberAlphabet(p, compression.begin());
	}
}
}//anonymous namespace

namespace encoding {
GadgetBuilder::GadgetBuilder(unsigned int alphabet_size, WorkingAutomaton::state_type gadget_state_estimate)
		: gadget(make_working(alphabet_size)) {
	//Every gadget has at least one state, and it's important that automaton
	//state 0 correspond to a gadget state (to accept the empty string).
	//For other states we can freely intermix the states that correspond to
	//gadget states and those that don't.
	gadget_state_estimate = std::max(gadget_state_estimate, 1u);
	for (WorkingAutomaton::state_type i = 0; i < gadget_state_estimate; ++i)
		translateState(i);
}

GadgetBuilder& GadgetBuilder::trans(WorkingAutomaton::state_type start, WorkingAutomaton::symbol_type from,
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

std::pair<unique_ptr<WorkingAutomaton>, unsigned int> GadgetBuilder::build(GadgetBuilderBuildArgs kwargs) const & {
	unique_ptr<WorkingAutomaton> p = gadget->clone();
	p->minimize();
	if (kwargs.compress_alphabet)
		compress_alphabet(*p);

	//The empty gadget.  The caller will probably discard it.
	if (p->state_size() == 1 && p->transition_size() == 0)
		return {std::move(p), std::numeric_limits<unsigned int>::max()};

	if (kwargs.mirror)
		//Awkwardly, this copies the gadget again, despite us having cloned it earlier.
		return ::mirror(*p);
	else {
		unsigned int rotation = canonicalize(*p, p->active_alphabet_size(), false);
		return {std::move(p), rotation};
	}
}

std::pair<unique_ptr<WorkingAutomaton>, unsigned int> GadgetBuilder::build(GadgetBuilderBuildArgs kwargs) && {
	gadget->minimize();
	if (kwargs.compress_alphabet)
		compress_alphabet(*gadget);

	//The empty gadget.  The caller will probably discard it.
	if (gadget->state_size() == 1 && gadget->transition_size() == 0)
		return {std::move(gadget), std::numeric_limits<unsigned int>::max()};

	if (kwargs.mirror)
		return ::mirror(*gadget);
	else {
		unsigned int rotation = canonicalize(*gadget, gadget->active_alphabet_size(), false);
		return {std::move(gadget), rotation};
	}
}

vector<unsigned int> GadgetBuilder::initialComponentGadgetStates() const {
	vector<unsigned int> ret;
	SCCs sccs = automaton::find_components(*gadget);
	for (auto state : sccs.component(sccs.find(0)))
		if (gadget->accept(state)) {
			auto it = std::find(gadgetStateToAutomatonState.begin(), gadgetStateToAutomatonState.end(), state);
			assert(it != gadgetStateToAutomatonState.end());
			ret.push_back(numeric_cast<unsigned int>(std::distance(gadgetStateToAutomatonState.begin(), it)));
		}
	return ret;
}

void GadgetBuilder::setGadgetState(unsigned int newInitial) {
	gadget->swapStateNumbers(0, gadgetStateToAutomatonState.at(newInitial));
	std::swap(gadgetStateToAutomatonState[newInitial],
			*std::find(gadgetStateToAutomatonState.begin(), gadgetStateToAutomatonState.end(), 0));
}

WorkingAutomaton::state_type GadgetBuilder::translateState(WorkingAutomaton::state_type gadgetState) {
	while (gadgetState >= gadgetStateToAutomatonState.size()) {
		gadgetStateToAutomatonState.push_back(gadget->addState());
		gadget->setAccept(gadgetStateToAutomatonState.back());
	}
	return gadgetStateToAutomatonState[gadgetState];
}
} //namespace encoding

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

auto count_accept_components(const AutomatonBase& a) {
	//There's something fishy with SCCs.  See GitHub issue #92.
	SCCs sccs = automaton::find_components(a);
	unsigned int count = 0;
	for (auto c : xrange(sccs.size()))
		for (auto s : sccs.component(c))
			if (a.accept(s)) {
				++count;
				break;
			}
	return count;
}


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
#ifndef NDEBUG
	//gdb can't render hopscotch_set well in core dumps, so make a vector copy
	MAYBE_UNUSED std::vector<GadgetEdge> all_edges_vector(edges.begin(), edges.end());
#endif
	for (const GadgetEdge& e : edges) {
		if (!edges.count(e.reverse()))
			dedges.push_back(e);
		else if (e < e.reverse())
			uedges.push_back(e);
		else if (e == e.reverse()) //nop edges are not stored, just counted
			++nop_edges;
	}
	assert(2*uedges.size() + nop_edges + dedges.size() == edges.size());
	return {std::move(uedges), std::move(dedges)};
}



struct LLSSEdgeCoder {
	unsigned int locations, states;
	LLSSEdgeCoder(unsigned int location, unsigned int state) : locations(location), states(state) {}
	std::uint64_t encode(const GadgetEdge& e) const {
		if constexpr (assertions_enabled) {
			if (!(e.from < locations && e.to < locations && e.start < states && e.end < states)) {
				fmt::print(stderr, "edge too large to encode: {}, coding for {} locations, {} states\n",
						e, locations, states);
				std::terminate();
			}
		}
		std::uint64_t i = ((std::uint64_t)e.from * locations * states * states) +
				(e.to * states * states) +
				(e.start * states) +
				e.end;
		if constexpr (assertions_enabled) {
			//This is an error in the coder logic, not the caller.
			if (!(i < max_value())) {
				fmt::print(stderr, "can't happen: coded edge too large? {} from {}, coding for {} locations, {} states\n",
						i, e, locations, states);
				std::terminate();
			}
		}
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
void write_compressed(const std::vector<GadgetEdge>& edges, const EdgeCoder& coder, PackWriter& writer) {
	std::uint64_t previous = 0;
	for (const GadgetEdge& e : edges) {
		std::uint64_t cur = coder.encode(e);
		writer.writeVarint(numeric_cast<unsigned int>(cur - previous));
		previous = cur;
	}
}

template<class EdgeCoder>
std::vector<GadgetEdge> read_compressed(PackReader& reader, unsigned int count, const EdgeCoder& coder) {
	std::vector<GadgetEdge> ret;
	std::uint64_t previous = 0;
	for (unsigned int i = 0; i < count; ++i) {
		std::uint64_t cur = reader.readVarint();
		cur += previous;
		ret.push_back(coder.decode(cur));
		previous = cur;
	}
	return ret;
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
namespace detail {
//See the comment in gadget-encoding-stats.hpp for the definition of this header.
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

using edge_coder = LLSSEdgeCoder;
} //namespace detail

std::vector<std::byte> encode(const automaton::WorkingAutomaton& a) {
	assert(a.canonical());
	auto&& [uedges, dedges] = deflate_slls(a);
	Stats stats = {};
	stats.locations = a.active_alphabet_size();
	stats.states = a.accept_size();
	stats.undirected_edges = numeric_cast<unsigned int>(uedges.size());
	stats.directed_edges = numeric_cast<unsigned int>(dedges.size());
	stats.components = count_accept_components(a);
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
	return std::move(builder).build().first;
}

std::pair<std::vector<GadgetEdge>, std::vector<GadgetEdge>> decode_to_slls(const std::byte* encoded_gadget, std::size_t length) {
	auto&& [stats, edgelist_begin] = detail::stats(encoded_gadget);
	detail::edge_coder coder(stats.locations, stats.states);
	//This length calculation (and the function's length parameter) is just for
	//error checking.  We know how many edges to read, just not how many bytes
	//they were encoded with.
	PackReader reader(edgelist_begin, encoded_gadget + length);
	std::vector<GadgetEdge> uedges = read_compressed(reader, stats.undirected_edges, coder);
	std::vector<GadgetEdge> dedges = read_compressed(reader, stats.directed_edges, coder);
	assert(reader.tell() == encoded_gadget + length);
	assert(!reader.overflow());
	return {std::move(uedges), std::move(dedges)};
}

} //namespace encoding