/*
 * File:   provenance.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 5, 2018, 2:49 AM
 */

#ifndef PROVENANCE_HPP
#define PROVENANCE_HPP

#include "numutils.hpp"
#include <fmt/format.h>

struct Provenance {
	static Provenance input(std::uint32_t index) {
		Provenance p = {};
		p.first = ALLONES_32;
		p.second = numeric_cast<std::uint8_t>(index);
		p.machineId = ALLONES_8;
		return p;
	}
	static Provenance combine(std::uint32_t left, std::uint32_t right,
			std::uint32_t leftSplice, std::uint32_t rightRotation) {
		Provenance p = {};
		p.first = left;
		p.second = numeric_cast<std::uint8_t>(right);
		p.leftSplice = numeric_cast<std::uint8_t>(leftSplice);
		p.rightRotation = numeric_cast<std::uint8_t>(rightRotation);
		p.machineId = ALLONES_8;
		return p;
	}
	//This overload is used for the connect immediately following a combine.
	static Provenance connect(const Provenance& combineData, std::uint32_t connection, std::uint32_t root) {
		Provenance p = combineData;
		p.connectPoint = numeric_cast<std::uint8_t>(connection);
		p.root = numeric_cast<decltype(p.root)>(root);;
		p.machineId = ALLONES_8;
		return p;
	}
	//This overload is used for connects directly from an input or from another combine.
	static Provenance connect(std::uint32_t parent, std::uint32_t connection, std::uint32_t root) {
		Provenance p = {};
		p.first = parent;
		p.second = ALLONES_8;
		p.connectPoint = numeric_cast<std::uint8_t>(connection);;
		p.root = numeric_cast<decltype(p.root)>(root);
		p.machineId = ALLONES_8;
		return p;
	}
	//only first (the non-input parent) needs a machine id
	std::uint32_t first;
	std::uint8_t second; //an input, so fits in a byte
	//TODO: could pack splice and rotation together into a byte
	std::uint8_t leftSplice;
	std::uint8_t rightRotation;
	std::uint8_t connectPoint;
	std::uint16_t root;
	std::uint8_t machineId;

	Provenance() = default;
	bool isInput() const {return first == ALLONES_32;}
	bool isCombine() const {return second != ALLONES_8;}
	bool isConnect() const {return !isInput();} //all combines are also connects
private:
	static constexpr std::uint8_t ALLONES_8 = std::numeric_limits<std::uint8_t>::max();
	static constexpr std::uint32_t ALLONES_32 = std::numeric_limits<std::uint32_t>::max();
};

std::ostream& operator<<(std::ostream&, Provenance&);

namespace fmt {
template<>
struct formatter<Provenance> {
	template <typename ParseContext>
	constexpr auto parse(ParseContext &ctx) {return ctx.begin();}
	template <typename FormatContext>
	auto format(const Provenance& p, FormatContext& ctx) {
		//The decimal format specifier is necessary because std::uint8_t is
		//also a character type.
		if (p.isInput())
			return format_to(ctx.begin(), "input {:d}", p.second);
		if (p.isCombine())
			return format_to(ctx.begin(), "combine {:d},{:d} at {:d} with {:d} at {:d}, connect {:d} at {:d} start {:d}",
					p.machineId, p.first, p.leftSplice, p.second, p.rightRotation, p.connectPoint, p.root);
		return format_to(ctx.begin(), "connect {:d},{:d} start {:d}",
					p.machineId, p.first, p.connectPoint, p.root);
	}
};
}

#endif /* PROVENANCE_HPP */

