// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#ifndef GADGET_SET_HPP
#define GADGET_SET_HPP

#include <vector>
#include <string>
#include <string_view>
#include <lmdb++.h>
#include <msgpack.hpp>

struct GadgetSet {
	std::vector<std::uint64_t> ids;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges; //inclusive, exclusive
	std::vector<std::string> names;
	MSGPACK_DEFINE_ARRAY(ids, ranges, names)
};

GadgetSet parse_gid_specs(const std::vector<std::string_view>& specs);
std::string format_gadget_set(const GadgetSet& gs);
std::vector<std::uint64_t> collect_initial_gadget_set(lmdb::env& env, const GadgetSet& gs);
std::vector<std::uint64_t> collect_initial_gadget_set(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& names, const GadgetSet& gs);

#endif /* GADGET_SET_HPP */

