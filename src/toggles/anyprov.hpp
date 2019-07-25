#ifndef ANYPROV_HPP
#define ANYPROV_HPP

#include "toggles-shared.hpp"

using std::vector;
using std::uint8_t;
using std::uint64_t;

enum class EdgeKind : unsigned char {
	combine = 0, connect = 1, close = 2, mirror = 3, source = 4
};
inline bool operator<(EdgeKind a, EdgeKind b) {
	return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
}
std::string_view name_for_kind(EdgeKind kind) {
	switch (kind) {
		case EdgeKind::combine: return "combine";
		case EdgeKind::connect: return "connect";
		case EdgeKind::close: return "close";
		case EdgeKind::mirror: return "mirror";
		case EdgeKind::source: return "source";
	}
	throw std::logic_error(fmt::format("bad kind: {}", static_cast<unsigned int>(kind)));
}
template<>
struct fmt::formatter<EdgeKind> : formatter<string_view> {
	template<typename FormatContext>
	auto format(const EdgeKind kind, FormatContext& ctx) {
		return fmt::formatter<string_view>::format(name_for_kind(kind), ctx);
	}
};

class AnyProv {
public:
	static AnyProv source(uint64_t output) {
		return {EdgeKind::source, output, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max()};
	}
	static AnyProv combine(uint64_t input1, uint64_t input2, const CombineEdge& e) {
		return {EdgeKind::combine, e.output, input1, input2, e.splice, e.rotation, e.connectPoint, e.canonicalizePermutation};
	}
	static AnyProv connect(uint64_t input1, const ConnectEdge& e) {
		return {EdgeKind::connect, e.output, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				e.connectPoint, e.canonicalizePermutation};
	}
	static AnyProv close(uint64_t input1, const SimpleEdge& e) {
		return {EdgeKind::close, e.output, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), e.canonicalizePermutation};
	}
	static AnyProv mirror(uint64_t input1, const SimpleEdge& e) {
		return {EdgeKind::mirror, e.output, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), e.canonicalizePermutation};
	}

	EdgeKind kind() const {
		return kind_;
	}
	uint64_t output() const {
		return output1_;
	}
	uint64_t input1() const {
		return input1_;
	}
	uint64_t input2() const {
		assert(input2_ != std::numeric_limits<uint64_t>::max());
		return input2_;
	}
	uint8_t splice() const {
		assert(splice_ != std::numeric_limits<uint8_t>::max());
		return splice_;
	}
	uint8_t rotation() const {
		assert(rotation_ != std::numeric_limits<uint8_t>::max());
		return rotation_;
	}
	uint8_t connectPoint() const {
		assert(connectPoint_ != std::numeric_limits<uint8_t>::max());
		return connectPoint_;
	}
	uint8_t canonicalizePermutation() const {
		return canonicalizePermutation_;
	}

	vector<uint64_t> inputs() const {
		//Returning a vector isn't great for perf, but is convenient.
		vector<uint64_t> r;
		if (input1_ != std::numeric_limits<uint64_t>::max())
			r.push_back(input1_);
		if (input2_ != std::numeric_limits<uint64_t>::max())
			r.push_back(input2_);
		return r;
	}
private:
	AnyProv(EdgeKind kind, uint64_t output1, uint64_t input1, uint64_t input2,
			uint8_t splice, uint8_t rotation, uint8_t connectPoint,
			uint8_t canonicalizeRotation) : output1_(output1), input1_(input1),
					input2_(input2), splice_(splice), rotation_(rotation),
					connectPoint_(connectPoint), canonicalizePermutation_(canonicalizeRotation), kind_(kind) {}
	std::uint64_t output1_, input1_, input2_;
	std::uint8_t splice_, rotation_, connectPoint_;
	std::uint8_t canonicalizePermutation_;
	//TODO: steal two bits from one of the other fields, or encode using special values of unused fields
	EdgeKind kind_;

	friend bool operator==(const AnyProv& a, const AnyProv& b) {
		return a.output1_ == b.output1_ && a.input1_ == b.input1_ &&
				a.input2_ == b.input2_ && a.splice_ == b.splice_ &&
				a.rotation_ == b.rotation_ && a.connectPoint_ == b.connectPoint_ &&
				a.canonicalizePermutation_ == b.canonicalizePermutation_ &&
				a.kind_ == b.kind_;
	}
};

template<>
struct fmt::formatter<AnyProv> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const AnyProv& p, FormatContext& ctx) {
		switch (p.kind()) {
			case EdgeKind::combine:
				return fmt::format_to(ctx.out(), "{} = combine {} splice {:d} with {} rotate {:d} connect at {:d} @{:d}",
						p.output(), p.input1(), p.splice(), p.input2(), p.rotation(),
						p.connectPoint(), p.canonicalizePermutation());
			case EdgeKind::connect:
				return fmt::format_to(ctx.out(), "{} = connect {} at {:d} @{:d}",
						p.output(), p.input1(), p.connectPoint(), p.canonicalizePermutation());
			case EdgeKind::close:
				return fmt::format_to(ctx.out(), "{} = close {} @{:d}",
						p.output(), p.input1(), p.canonicalizePermutation());
			case EdgeKind::mirror:
				return fmt::format_to(ctx.out(), "{} = mirror {} @{:d}",
						p.output(), p.input1(), p.canonicalizePermutation());
			case EdgeKind::source:
				return fmt::format_to(ctx.out(), "{} = source", p.output());
			default:
				//TODO: We'd like to dump the other members to help track down
				//the corruption, but we'd hit the asserts in the methods.
				//Figure out how friending a future full specialization works,
				//then print the members directly.
				return fmt::format_to(ctx.out(), "unknown AnyProv kind {}", static_cast<unsigned char>(p.kind()));
		}
	}
};

#endif /* ANYPROV_HPP */

