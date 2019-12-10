#include "precompiled.hpp"
#include "gadget-encoding.hpp"
#include "../select-by-id.hpp"
#include "transform_reduce.hpp"
#include "intervals.hpp"
#include "stringutils.hpp"

using std::vector;
using std::uint64_t;
using std::pair;
using namespace std::literals::string_view_literals;
using encoding::GadgetEdge;

void name_alternating_leaky_directed_crossovers(uint64_t gadget_id,
		const vector<GadgetEdge>& uedges, const vector<GadgetEdge>& dedges,
		vector<pair<std::string, uint64_t>>& results) {

}

int scan_mode(std::string_view db_path, vector<std::string_view>& args) {
	unsigned int num_threads = 0;
	for (std::size_t i = 0; i < args.size(); ++i)
		if (args[i] == "--threads"sv)
			num_threads = from_string<unsigned int>(args[++i]);
		else {
			fmt::print(stderr, "ERROR: unknown option {}\n", args[i]);
			std::exit(2);
		}

	if (!num_threads)
		num_threads = std::thread::hardware_concurrency();

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}

	vector<pair<uint64_t, uint64_t>> every_gadget_ever = {{1, std::numeric_limits<uint64_t>::max()}};
	vector<vector<pair<uint64_t, uint64_t>>> tasks = interval_chunk(
			every_gadget_ever.begin(), every_gadget_ever.end(), 250000);
	vector<pair<std::string, uint64_t>> names = transform_reduce(std::move(tasks), num_threads,
	[&](vector<pair<uint64_t, uint64_t>> task) {
		vector<pair<std::string, uint64_t>> results;
		vector<pair<uint64_t, vector<std::byte>>> data = select_gadget_id_to_data(env, gadget_hashtable, gadget_index, task);
		while (!data.empty()) {
			pair<vector<GadgetEdge>, vector<GadgetEdge>> edges = encoding::decode_to_slls(
					data.back().second.data(), data.back().second.size());
			name_alternating_leaky_directed_crossovers(data.back().first, edges.first, edges.second, results);
			data.pop_back();
		}
		return results;
	}, [](vector<pair<std::string, uint64_t>>&& left, vector<pair<std::string, uint64_t>>&& right) {
		vector<pair<std::string, uint64_t>> result(std::move(left));
		vector<pair<std::string, uint64_t>> rest(std::move(right)); //ensure memory is released
		result.insert(result.end(), std::move_iterator(rest.begin()), std::move_iterator(rest.end()));
		return result;
	});

	return 0;
}