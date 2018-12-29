#include "precompiled.hpp"
#include "automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
#include "hopscotch/hopscotch_set.h"
#include "hopscotch/hopscotch_map.h"
#include "msgpack.hpp"
#include <cstdio>

using namespace automaton;
using std::size_t;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

class GadgetBuilder {
public:
	GadgetBuilder(unsigned int alphabet_size, WorkingAutomaton::state_type preinitialized_states = 0) : gadget(make_working(alphabet_size)) {
		for (WorkingAutomaton::state_type i = 0; i < preinitialized_states; ++i)
			translateState(i);
	}
	GadgetBuilder& trans(WorkingAutomaton::state_type start, WorkingAutomaton::symbol_type from,
			WorkingAutomaton::symbol_type to, WorkingAutomaton::state_type end) {
		assert(gadget->accept(translateState(start)));
		assert(gadget->accept(translateState(end)));
		WorkingAutomaton::state_type t = gadget->addState();
		gadget->addTrans(translateState(start), from, t);
		gadget->addTrans(t, to, translateState(end));
		return *this;
	}
	unique_ptr<WorkingAutomaton> build() {
		branchToAnyAcceptState(*gadget);
		gadget->minimize();
		auto alpha = gadget->alphabet_size(), activealpha = gadget->active_alphabet_size();
		auto nop = make_nop(alpha, activealpha);
		gadget = shuffleAccept(*gadget, *nop, alpha);
		gadget->minimize();
		acceptingClosure(*gadget, activealpha);
		branchToAnyAcceptState(*gadget);
		gadget->minimize();
		canonicalize(*gadget, activealpha);
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
	static void branchToAnyAcceptState(WorkingAutomaton& a) {
		StateSet accepting;
		for (AutomatonBase::state_type s = 0; s < a.state_size(); ++s)
			if (a.accept(s))
				accepting.insert_absent(s);
		setInitialStates(a, accepting);
	}
	static void setInitialStates(WorkingAutomaton& a, const StateSet& initialStates) {
		using state_type = WorkingAutomaton::state_type;
		state_type s = a.addState();
		for (state_type t : initialStates)
			a.addEpsilon(s, t);
		a.swapStateNumbers(0, s);
	}
	static unique_ptr<WorkingAutomaton> make_nop(unsigned int alphabet_size, unsigned int locations) {
		if (locations > alphabet_size) return nullptr;
		//GadgetBuilder calls prepare which calls make_nop, so we have to do this
		//manually to break the cycle.
		unique_ptr<WorkingAutomaton> a = make_working(alphabet_size);
		a->addState();
		a->setAccept(0, true);
		for (unsigned int i = 0; i < locations; ++i) {
			auto s = a->addState();
			a->addTrans(0, i, s);
			a->addTrans(s, i, 0);
		}
		return a;
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
	if (!alphabetSize) {
		//Size-to-fit by finding the largest used symbol.
		for (auto e : gadget.uedges)
			alphabetSize = std::max({alphabetSize, e.from, e.to});
		for (auto e : gadget.dedges)
			alphabetSize = std::max({alphabetSize, e.from, e.to});
		++alphabetSize;
	}
	GadgetBuilder b(alphabetSize);
	for (auto e : gadget.uedges)
		b.trans(e.start, e.from, e.to, e.end).trans(e.end, e.to, e.from, e.start);
	for (auto e : gadget.dedges)
		b.trans(e.start, e.from, e.to, e.end);
	return b.build();
}

SLLS deflate_slls(const AutomatonBase& a) {
	SLLS gadget;
	auto state_size = a.state_size(), accept_size = a.accept_size();
	auto activealpha = a.activeAlphabet();
	if (accept_size == 2)
		//Accept size should be 1 (stateless, e.g., nops and splits) or >= 3
		//(2+ states, plus one for superposition).
		throw std::logic_error("accept size 2?!");

	unsigned int gadgetStates = 0;
	tsl::hopscotch_map<AutomatonBase::state_type, unsigned int> autoToGadget; //maps automaton states to gadget states
	tsl::hopscotch_set<GadgetEdge> edges;
	for (AutomatonBase::state_type start = 0; start < state_size; ++start) {
		if (!a.accept(start)) continue;
		if (accept_size != 1 && start == 0) continue; //ignore superposition pseudostate
		for (AutomatonBase::symbol_type from : activealpha) {
			auto middle = a.stepDeterministic(start, from);
			if (middle) {
				assert(!a.accept(*middle));
				for (AutomatonBase::symbol_type to : activealpha) {
					auto end = a.stepDeterministic(*middle, to);
					if (end) {
						assert(a.accept(*end));
						if (accept_size != 1 && *end == 0) continue; //ignore superposition pseudostate
						//TODO: are nops not in the database?  their SL graph is empty, and we minimize both states and locations...
						if (start == *end && from == to) continue; //skip nop edges
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

	for (const GadgetEdge& e : edges) {
		if (!edges.count(e.reverse()))
			gadget.dedges.push_back(e);
		else if (e < e.reverse()) //only the lesser of the pair
			gadget.uedges.push_back(e);
	}
	assert(2*gadget.uedges.size() + gadget.dedges.size() == edges.size());
	return gadget;
}

/**
 * A gadget is uniquely identified in the database as a byte array starting with
 * a count of states, locations, undirected edges and directed edges, each as a
 * varint, followed by the edges.  The undirected edges (if any) precede the
 * directed edges (if any).  Each endpoint is s*locations+l as a varint.  Edges
 * are sorted in natural tuple order before being compressed; undirected edges
 * are encoded as the lesser of the pair of edges they represent.
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
	unsigned int states, locations, uedges, dedges;
	std::vector<std::byte> edges;
	MSGPACK_DEFINE_ARRAY(states, locations, uedges, dedges, edges)
};


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
	PackReader(const vector<std::byte>& data) : data_(data) {}
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


OutputRow deflate_outputrow(const AutomatonBase& a) {
	SLLS slls = deflate_slls(a);
	unsigned int states = a.accept_size() == 1 ? 1 : a.accept_size() - 1;
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
	return {states, locations,
			numeric_cast<unsigned int>(slls.uedges.size()), numeric_cast<unsigned int>(slls.dedges.size()),
			std::move(data.data)};
}

/**
 * Canonicalizes a gadget in SLLS format, returning in database row format.
 * Intended for use when loading human-readable gadget definitions into the
 * database.
 */
std::vector<OutputRow> canonicalize_from_slls(SLLS gadget) {
	unique_ptr<WorkingAutomaton> a = inflate_slls(gadget);
	unique_ptr<WorkingAutomaton> b = mirror(*a);
	//We already canonicalized it when we built it; now we just have to pack it
	//into row format.  If it's chiral, we return both enantiomers.
	if (*a != *b) {
		OutputRow la = deflate_outputrow(*a), lb = deflate_outputrow(*b);
		//Order them arbitrarily but consistently.
		assert(la.edges != lb.edges);
		if (std::lexicographical_compare(lb.edges.begin(), lb.edges.end(), la.edges.begin(), la.edges.end()))
			std::swap(la, lb);
		return {std::move(la), std::move(lb)};
	} else
		return {deflate_outputrow(*a)};
}


template<typename T>
struct callable_traits : callable_traits<decltype(&T::operator())> {};

template<typename C, typename R, typename... Args>
struct callable_traits<R(C::*)(Args...)> : callable_traits<R(*)(Args...)> {};

template<typename C, typename R, typename... Args>
struct callable_traits<R(C::*)(Args...) const> : callable_traits<R(*)(Args...)> {};

template<typename R, typename... Args>
struct callable_traits<R(*)(Args...)> {
    using result_type = R;
    using arity = std::integral_constant<std::size_t, sizeof...(Args)>;
    using args_type = std::tuple<typename std::decay<Args>::type...>;
	template<std::size_t idx>
	using arg = typename std::tuple_element<idx, args_type>::type;
};

struct bad_arity {size_t expected, actual;};
template<auto f>
msgpack::object_handle handler_adapter(const msgpack::object& arg_array) {
	using args_type = typename callable_traits<decltype(f)>::args_type;
	if (arg_array.via.array.size != std::tuple_size<args_type>::value)
		throw bad_arity{std::tuple_size<args_type>::value, arg_array.via.array.size};
	auto zone = std::make_unique<msgpack::zone>();
	msgpack::object result(std::apply(f, arg_array.as<args_type>()), *zone);
	return {std::move(result), std::move(zone)};
}

using handler_ptr = msgpack::object_handle(*)(const msgpack::object&);
const std::pair<string_view, handler_ptr> handlers[] = {
	{"canonicalize"sv, &handler_adapter<canonicalize_from_slls>},
};

msgpack::sbuffer pack_success(uint32_t seq_no, const msgpack::object result) {
	msgpack::sbuffer buffer;
	std::tuple<uint8_t, uint32_t, msgpack::type::nil_t, msgpack::object>
			response(1, seq_no, msgpack::type::nil_t{}, result);
	msgpack::pack(buffer, response);
	return buffer;
}

msgpack::sbuffer pack_error(uint32_t seq_no, const std::string& error) {
	msgpack::sbuffer buffer;
	std::tuple<uint8_t, uint32_t, std::string, msgpack::type::nil_t>
			response(1, seq_no, error, msgpack::type::nil_t{});
	msgpack::pack(buffer, response);
	return buffer;
}

msgpack::sbuffer dispatch(msgpack::object_handle hcmd) {
	auto [msg_type, seq_no, command, arg_array] = hcmd.get().as<std::tuple<uint8_t, uint32_t, std::string, msgpack::object>>();
	if (msg_type != 0) {
		fmt::print(stderr, "bad msg_type {}\n", msg_type);
		//TODO: send error response to stdout?  but this is a clearly-broken case
		std::exit(1);
	}
	if (arg_array.type != msgpack::type::ARRAY)
		return pack_error(seq_no,
				fmt::format("argument for command '{}' not an array, actually a {}",
				command, arg_array.type));

	handler_ptr handler = nullptr;
	for (auto [name, h] : handlers)
		if (command == name)
			handler = h;
	if (!handler)
		return pack_error(seq_no, fmt::format("no handler for command '{}'", command));

	try {
		msgpack::object_handle result = handler(arg_array);
		return pack_success(seq_no, result.get());
	} catch (bad_arity& e) {
		return pack_error(seq_no, fmt::format("bad arity for command '{}': expected {}, got {}",
				command, e.expected, e.actual));
	} catch (std::exception& e) {
		return pack_error(seq_no, fmt::format("command '{}' threw an exception: {}", command, e.what()));
	} catch (...) {
		return pack_error(seq_no, fmt::format("command '{}' threw an unusual exception", command));
	}
	//We only get here if we unwind out of one of the above catch blocks.  We'll
	//probably just throw again for the same reason, but we might as well try:
	return pack_error(seq_no, "somehow threw exception while reporting error?");
}

vector<char> exhaust_stdin() {
	vector<char> data;
	data.resize(4096, 0);
	size_t index = 0;
	while (true) {
		size_t count = data.size() - index;
		size_t bytes_read = std::fread(&data[index], sizeof(unsigned char), count, stdin);
		if (bytes_read != count) {
			if (std::feof(stdin))
				return data;
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

void write_output(const char* data, size_t size) {
	//TODO: optional compression based on command-line option
	size_t index = 0;
	while (index < size) {
		size_t count = size - index;
		size_t bytes_written = std::fwrite(data+index, sizeof(char), count, stdout);
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

	msgpack::sbuffer response = dispatch(read_input());
	write_output(response.data(), response.size());

	return 0;
}