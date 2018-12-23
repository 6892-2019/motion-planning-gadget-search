#include "precompiled.hpp"
#include "automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
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
	GadgetBuilder(unsigned int alphabet_size, WorkingAutomaton::state_type preinitialized_states) : gadget(make_working(alphabet_size)) {
		for (WorkingAutomaton::state_type i = 0; i < preinitialized_states; ++i)
			translateState(i);
	}
	GadgetBuilder& trans(WorkingAutomaton::state_type start, WorkingAutomaton::symbol_type from,
			WorkingAutomaton::symbol_type to, WorkingAutomaton::state_type end) {
		assert(gadget->accept(start));
		assert(gadget->accept(end));
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
		canonicalizeQ(*gadget);
		return std::move(gadget);
	}
private:
	unique_ptr<WorkingAutomaton> gadget;
	vector<WorkingAutomaton::state_type> gadgetStateToAutomatonState;

	WorkingAutomaton::state_type translateState(WorkingAutomaton::state_type gadgetState) {
		while (gadgetState < gadgetStateToAutomatonState.size()) {
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
	static void canonicalizeQ(WorkingAutomaton& a) {
		//This is ugh, but canonicalizeRenumber is a template and so can't easily be
		//moved onto WorkingAutomaton.
		switch (a.alphabet_size()) {
	#define GADGETDEFS_CANONICALIZE_CASE(N) case N: canonicalize(static_cast<Automaton<N>&>(a), a.active_alphabet_size()); break;
			GADGETDEFS_CANONICALIZE_CASE(1)
			GADGETDEFS_CANONICALIZE_CASE(2)
			GADGETDEFS_CANONICALIZE_CASE(3)
			GADGETDEFS_CANONICALIZE_CASE(4)
			GADGETDEFS_CANONICALIZE_CASE(5)
			GADGETDEFS_CANONICALIZE_CASE(6)
			GADGETDEFS_CANONICALIZE_CASE(7)
			GADGETDEFS_CANONICALIZE_CASE(8)
			GADGETDEFS_CANONICALIZE_CASE(9)
			GADGETDEFS_CANONICALIZE_CASE(10)
			GADGETDEFS_CANONICALIZE_CASE(11)
			GADGETDEFS_CANONICALIZE_CASE(12)
			GADGETDEFS_CANONICALIZE_CASE(13)
			GADGETDEFS_CANONICALIZE_CASE(14)
			GADGETDEFS_CANONICALIZE_CASE(15)
			GADGETDEFS_CANONICALIZE_CASE(16)
	#undef GADGETDEFS_CANONICALIZE_CASE
		default:
			std::cout << "unhandled canonicalize: " << typeid(a).name();
			std::terminate();
		}
	}
};

//{"type": "slls", "uedges": [[0, 0, 1, 1]], "dedges": [[0, 0, 0, 0]]}
//unique_ptr<WorkingAutomaton> inflate_edgelist(json gadget, unsigned int alphabetSize) {
//	GadgetBuilder b(alphabetSize);
//
//	return b.build();
//}

int canonicalizeF(int a, int b) {
	return a + b;
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
	{"canonicalize"sv, &handler_adapter<canonicalizeF>},
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