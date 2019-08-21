#include "precompiled.hpp"
#include "ops.hpp"
#include "../select-by-id.hpp"
#include "ioutils.hpp"
#include "stringutils.hpp"
#include "farmhash-util.hpp"
#include "hopscotch/hopscotch_map.h"

using std::vector;
using std::pair;
using std::uint64_t;
using namespace automaton;
using SymbolSet = AutomatonBase::SymbolSet;

//Derived from code copied from ops.cpp.
SymbolSet combine_deleted_symbols(const Automaton<16>& la, const Automaton<16>& ra,
		unsigned int splice, unsigned int rotation, unsigned int connectPoint) {
	AutomatonBase::state_type leftLocations = la.active_alphabet_size();
	AutomatonBase::state_type rightLocations = ra.active_alphabet_size();
	using symbol_type = WorkingAutomaton::symbol_type;
	std::array<symbol_type, Automaton<16>::alphabet_size_v> slide;
	Automaton<16> shiftedRight = ra;
	std::iota(slide.begin(), slide.end(), 0);
	std::rotate(slide.rbegin(), slide.rbegin()+leftLocations, slide.rend());
	shiftedRight.renumberAlphabet(slide.data());
	Automaton<16> shuffled = automaton::shuffleAccept(la, shiftedRight);
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

	for (unsigned int ll = 0; ll+1 < splice; ++ll)
		std::swap(slide[ll], slide[ll+rightLocations]);
	std::iota(slide.begin()+splice, slide.begin()+splice+rightLocations, leftLocations);
	std::rotate(slide.begin()+splice, slide.begin()+splice+rotation, slide.begin()+splice+rightLocations);
	Automaton<16> permuted = shuffled;
	permuted.permuteAlphabet(slide.data());
	return connect_deleted_symbols(permuted, connectPoint);
}

int deleted_locations_mode(std::string_view db_path, std::string_view input_file, std::string_view output_file) {
	vector<std::string> lines = readAllLines(std::string(input_file));
	vector<uint64_t> gids;
	for (std::string& x : lines) {
		vector<std::string::size_type> commata;
		for (std::string::size_type i = x.find(','); i != std::string::npos; i = x.find(',', i+1))
			commata.push_back(i);

		//This splitting should be using stringutils.hpp
		gids.push_back(to_uint64(x.substr(0, commata[0])));
		gids.push_back(to_uint64(x.substr(commata[1]+1, commata[2]-commata[1]-1)));
		if (x.find("combine") != std::string::npos)
			gids.push_back(to_uint64(x.substr(commata[2]+1, commata[3]-commata[2]-1)));
	}
	std::sort(gids.begin(), gids.end());
	gids.erase(std::unique(gids.begin(), gids.end()), gids.end());

	tsl::hopscotch_map<uint64_t, vector<std::byte>, farmhash_hash> gadget_data;
	{
		lmdb::env env = lmdb::env::create();
		env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
		env.set_max_dbs(64);
		env.open(std::string(db_path).c_str(), MDB_RDONLY | MDB_NORDAHEAD);
		for (pair<uint64_t, vector<std::byte>>& p : select_gadget_id_to_data(env, gids))
			gadget_data.try_emplace(p.first, std::move(p.second));
	}

	vector<std::string> responses;
	for (std::string& x : lines) {
		if (x.find("combine") != std::string::npos) {
			uint64_t output, input1, input2;
			unsigned int splice, rotation, connectPoint;
			std::sscanf(x.c_str(), "%lu,combine,%lu,%lu,%u,%u,%u",
					&output, &input1, &input2, &splice, &rotation, &connectPoint);
			const auto& output_data = gadget_data.at(output);
			const auto& input1_data = gadget_data.at(input1);
			const auto& input2_data = gadget_data.at(input2);
			if (encoding::locations(output_data.data()) == encoding::locations(input1_data.data()) +
					encoding::locations(input2_data.data()) - 2)
				continue; //no deleted symbols
			SymbolSet dels = combine_deleted_symbols(
					*encoding::decode<16>(input1_data), *encoding::decode<16>(input2_data),
					splice, rotation, connectPoint);
			if (!dels.empty())
				responses.push_back(fmt::format("{} {}", output, fmt::join(dels, " ")));
		} else if (x.find("connect") != std::string::npos) {
			uint64_t output, input;
			unsigned int connectPoint;
			std::sscanf(x.c_str(), "%lu,connect,%lu,%u", &output, &input, &connectPoint);
			const auto& output_data = gadget_data.at(output);
			const auto& input_data = gadget_data.at(input);
			if (encoding::locations(output_data.data()) == encoding::locations(input_data.data()) - 2)
				continue; //no deleted symbols
			SymbolSet dels = connect_deleted_symbols(*encoding::decode(input_data.data(), input_data.size()), connectPoint);
			if (!dels.empty())
				responses.push_back(fmt::format("{} {}", output, fmt::join(dels, " ")));
		} else
			throw std::runtime_error(x);
	}

	writeAllLines(std::string(output_file), responses);
	return 0;
}