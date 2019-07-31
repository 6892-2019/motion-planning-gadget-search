#include "precompiled.hpp"
#include "lmdb-interval-list.hpp"
#include "intervals.hpp"

using std::vector;
using std::pair;
using std::uint64_t;

/**
 * Returns a view to an interval list in the database, or a pair of nullptr if
 * the key does not exist.  Throws if the value has the wrong size.
 */
pair<const pair<uint64_t, uint64_t>*, const pair<uint64_t, uint64_t>*>
view_interval_list(lmdb::txn& txn, lmdb::dbi& database, std::string_view key) {
	std::string_view value;
	if (!database.get(txn, key, value))
		return {nullptr, nullptr};
	//TODO: this kind of logic is pretty common, but with different types/throw info;
	//maybe there's a helper that returns a range or throws via a lambda?
	if (value.size() % sizeof(pair<uint64_t, uint64_t>) != 0)
		//There doesn't seem to be a way to get a dbi's name, so the best wa can
		//give is the handle number.
		throw std::logic_error(fmt::format("view_interval_list: db {} key {} has value length {} (not a multiple of {})",
				database.handle(), key, value.size(), sizeof(pair<uint64_t, uint64_t>)));
	const pair<uint64_t, uint64_t>* first = reinterpret_cast<const pair<uint64_t, uint64_t>*>(value.data());
	const pair<uint64_t, uint64_t>* last = first + value.size() / sizeof(pair<uint64_t, uint64_t>);
	return {first, last};
}

vector<pair<uint64_t, uint64_t>> subtract_interval_list(
		lmdb::txn& txn, lmdb::dbi& database, std::string_view key,
		const vector<pair<uint64_t, uint64_t>>& intervals) {
	auto list = view_interval_list(txn, database, key);
	if (!list.first)
		return intervals;
	return interval_difference(intervals.begin(), intervals.end(), list.first, list.second);
}

vector<pair<uint64_t, uint64_t>> intersect_interval_list(
		lmdb::txn& txn, lmdb::dbi& database, std::string_view key,
		const vector<pair<uint64_t, uint64_t>>& intervals) {
	auto list = view_interval_list(txn, database, key);
	if (!list.first)
		return intervals;
	return interval_intersection(intervals.begin(), intervals.end(), list.first, list.second);
}

vector<pair<uint64_t, uint64_t>> write_interval_list_union(
		lmdb::txn& txn, lmdb::dbi& database, std::string_view key,
		const vector<pair<uint64_t, uint64_t>>& intervals) {
	auto list = view_interval_list(txn, database, key);
	auto result = list.first ?
		interval_union(list.first, list.second, intervals.begin(), intervals.end()) :
		intervals; //if key not present, our intervals are the first
	std::string_view value(reinterpret_cast<const char*>(result.data()),
			result.size() * sizeof(pair<uint64_t, uint64_t>));
	if (!database.put(txn, key, value))
		throw std::logic_error(fmt::format("can't happen? write_interval_list_union db {} key {} with {} intervals ({} bytes)",
				key, result.size(), value.size()));
	//We may as well return this given we computed it.
	return result;
}