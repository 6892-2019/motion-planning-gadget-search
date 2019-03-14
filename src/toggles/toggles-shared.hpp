/*
 * File:   toggles-shared.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on March 14, 2019, 12:27 AM
 */

#ifndef TOGGLES_SHARED_HPP
#define TOGGLES_SHARED_HPP

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

#endif /* TOGGLES_SHARED_HPP */

