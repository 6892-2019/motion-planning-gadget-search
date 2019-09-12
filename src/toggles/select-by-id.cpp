#include "precompiled.hpp"
#include "select-by-id.hpp"
#include "intervals.hpp"
#include "proj_compare.hpp"

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;

std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, const std::vector<std::uint64_t>& gids) {
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}
	return select_gadget_id_to_data(env, gadget_hashtable, gadget_index, gids);
}
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals) {
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}
	return select_gadget_id_to_data(env, gadget_hashtable, gadget_index, gid_intervals);
}

namespace {
//This is separate from select_gadget_id_to_value both to factor it out of the
//template and because the vector<uint64_t> (i.e., non-interval) overload of
//select_gadget_id_to_data needs to do something slightly different.
std::vector<std::pair<std::uint64_t, std::uint64_t>> select_gadget_id_to_hash(
		lmdb::txn& txn, lmdb::dbi& gadget_index, const vector<pair<uint64_t, uint64_t>>& gid_intervals) {
	//gadget_index is a list of optimally-sized pages of hashes, where the key
	//is the index of the last hash on that page.  The key is inclusive, so the
	//last key in the database is the last valid gadget id.  This results in
	//awkward indexing but means MDB_SET_RANGE brings us to the right page.
	vector<pair<uint64_t, std::size_t>> id_to_hash;
	lmdb::cursor cur = lmdb::cursor::open(txn, gadget_index);
	for (pair<uint64_t, uint64_t> p : gid_intervals) {
		uint64_t id = p.first;
		if (id == 0)
			throw std::logic_error("wef");
		while (id < p.second) { //interval might span pages
			std::string_view key = lmdb::to_sv(id), value;
			if (!cur.get(key, value, MDB_SET_RANGE))
				throw std::logic_error(fmt::format("id {} (from interval {}) beyond end of gadget_index", id, p));
			uint64_t last_id_on_page = lmdb::from_sv<uint64_t>(key);
			uint64_t first_id_on_page = last_id_on_page - value.size()/sizeof(uint64_t) + 1;
			const char* data = value.data() + (id - first_id_on_page) * sizeof(uint64_t);
			while (id < p.second && data != value.end()) {
				uint64_t hash;
				std::memcpy(&hash, data, sizeof(uint64_t));
				id_to_hash.emplace_back(id, hash);
				++id;
				data += sizeof(uint64_t);
			}
		}
	}
	return id_to_hash;
}

template<class ValueExtractor, class V = decltype(ValueExtractor()(""sv))>
std::vector<std::pair<std::uint64_t, V>> select_gadget_id_to_value(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& gadget_hashtable,
		vector<pair<uint64_t, std::size_t>>&& id_to_hash) {
	std::sort(id_to_hash.begin(), id_to_hash.end(), proj_less<1>());
	std::vector<std::pair<std::uint64_t, V>> ret;
	ret.reserve(id_to_hash.size());
	lmdb::cursor cur = lmdb::cursor::open(txn, gadget_hashtable);
	for (auto& p : id_to_hash) {
		std::string_view key = lmdb::to_sv(p.second), value;
		if (!cur.get(key, value, MDB_SET))
			throw std::logic_error(fmt::format("hash {} not found (for id {})", p.second, p.first));
		//The last 8 bytes are the id.  Check them, then don't return them.
		uint64_t appended_id = lmdb::from_sv<uint64_t>(value.substr(value.size()-8));
		if (appended_id != p.first)
			throw std::logic_error(fmt::format("looked up hash {} for id {}, but hashtable gives id {}",
					p.second, p.first, appended_id));
		value.remove_suffix(8);
		ret.emplace_back(p.first, ValueExtractor()(value));
	}
	return ret;
}

struct gadget_copier {
	std::vector<std::byte> operator()(std::string_view gadget_hashtable_value) {
		vector<std::byte> value_copy;
		value_copy.resize(gadget_hashtable_value.size());
		std::memcpy(value_copy.data(), gadget_hashtable_value.data(), gadget_hashtable_value.size());
		return value_copy;
	}
};
} //anonymous namespace

std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::uint64_t>& gids) {
	interval_accumulator<uint64_t> accum(128);
	for (uint64_t i : gids)
		accum(i);
	return select_gadget_id_to_data(env, gadget_hashtable, gadget_index, std::move(accum).finish());
}
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals) {
	assert(std::is_sorted(gid_intervals.begin(), gid_intervals.end()));
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto id_to_hash = select_gadget_id_to_hash(txn, gadget_index, gid_intervals);
	auto ret = select_gadget_id_to_value<gadget_copier>(env, txn, gadget_hashtable, std::move(id_to_hash));
	txn.commit();
	return ret;
}


namespace {
struct stats_extractor {
	encoding::Stats operator()(std::string_view gadget_hashtable_value) {
		return encoding::stats(reinterpret_cast<const std::byte*>(gadget_hashtable_value.data()));
	}
};
}//anonymous namespace

std::vector<std::pair<std::uint64_t, encoding::Stats>> select_gadget_id_to_stats(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = select_gadget_id_to_value<stats_extractor>(env, txn, gadget_hashtable,
			select_gadget_id_to_hash(txn, gadget_index, gid_intervals));
	txn.commit();
	return ret;
}

std::vector<std::pair<std::uint64_t, encoding::Stats>> select_gadget_id_to_stats(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::uint64_t>& gids) {
	interval_accumulator<uint64_t> accum(128);
	for (uint64_t i : gids)
		accum(i);
	return select_gadget_id_to_stats(env, gadget_hashtable, gadget_index, std::move(accum).finish());
}

std::vector<std::pair<std::uint64_t, encoding::Stats>> select_gadget_id_to_stats(
		lmdb::env& env, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals) {
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}
	return select_gadget_id_to_stats(env, gadget_hashtable, gadget_index, gid_intervals);
}


uint64_t get_current_max_gadget_id(lmdb::env& env) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_current_max_gadget_id(txn);
	txn.commit();
	return ret;
}
uint64_t get_current_max_gadget_id(lmdb::env& env, lmdb::dbi& gadget_index) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_current_max_gadget_id(txn, gadget_index);
	txn.commit();
	return ret;
}
uint64_t get_current_max_gadget_id(lmdb::txn& txn) {
	lmdb::dbi gadget_index = lmdb::dbi::open(txn, "gadget_index");
	return get_current_max_gadget_id(txn, gadget_index);
}
uint64_t get_current_max_gadget_id(lmdb::txn& txn, lmdb::dbi& gadget_index) {
	//Duplicated from selsert_gadget_by_data.
	lmdb::cursor index_cur = lmdb::cursor::open(txn, gadget_index);
	std::string_view last_id_view;
	if (index_cur.get(last_id_view, MDB_LAST))
		return lmdb::from_sv<std::uint64_t>(last_id_view);
	return 0; //note that 0 is not the id of any actual gadget
}