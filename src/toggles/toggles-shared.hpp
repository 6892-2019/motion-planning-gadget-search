/*
 * File:   toggles-shared.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on March 14, 2019, 12:27 AM
 */

#ifndef TOGGLES_SHARED_HPP
#define TOGGLES_SHARED_HPP

#include "database.hpp"
#include <msgpack.hpp>

struct DatabaseOperationStatistics {
	std::size_t pruned_locally, pruned_database, novel_gadgets, edges;
	DatabaseOperationStatistics& operator+=(const DatabaseOperationStatistics& o) {
		pruned_locally += o.pruned_locally;
		pruned_database += o.pruned_database;
		novel_gadgets += o.novel_gadgets;
		edges += o.edges;
		return *this;
	}
	MSGPACK_DEFINE(pruned_locally, pruned_database, novel_gadgets, edges)
};

struct GadgetSet {
	std::vector<std::uint64_t> ids;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges; //inclusive, exclusive
	std::vector<std::string> names;
	MSGPACK_DEFINE_ARRAY(ids, ranges, names)
};

GadgetSet parse_gid_specs(const std::vector<std::string_view>& specs);
std::string format_gadget_set(const GadgetSet& gs);
std::vector<std::uint64_t> collect_initial_gadget_set(pqxx::connection& conn, const GadgetSet& gs);



/**
 * A row (minus the primary key) of an edge in the connect_provenance table.  We
 * use smaller types to save space.
 */
struct ConnectProvenance {
	std::uint64_t input1;
	std::uint32_t output1;
	std::uint8_t connectPoint;
	std::uint8_t canonicalizePermutation;
	MSGPACK_DEFINE_ARRAY(input1, output1, connectPoint, canonicalizePermutation)
};

/**
 * A row (minus the primary key) of an edge in the combine_provenance table.  We
 * use smaller types to save space.
 */
struct CombineProvenance {
	std::uint64_t input1, input2;
	std::uint32_t output1;
	std::uint8_t splice, rotation, connectPoint;
	std::uint8_t canonicalizePermutation;
	MSGPACK_DEFINE_ARRAY(input1, input2, output1, splice, rotation, connectPoint, canonicalizePermutation)
};

struct SimpleProvenance {
	std::uint64_t input1;
	std::uint32_t output1;
	std::uint8_t canonicalizePermutation;
	MSGPACK_DEFINE_ARRAY(input1, output1, canonicalizePermutation)
};



std::string build_select_gadget_id_to_data_immediate(const std::vector<std::uint64_t>& gids);

std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		pqxx::connection& conn, const std::vector<std::uint64_t>& gids);

#endif /* TOGGLES_SHARED_HPP */

