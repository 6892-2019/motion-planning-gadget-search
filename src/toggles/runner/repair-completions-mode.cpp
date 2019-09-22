#include "precompiled.hpp"
#include "../select-by-id.hpp"
#include "../predicates.hpp"
#include "../completions.hpp"
#include "intervals.hpp"
#include "stringutils.hpp"
#include "stopwatch.hpp"
#include "transform_reduce.hpp"

using std::vector;
using std::uint64_t;
using std::pair;
using namespace std::literals::string_view_literals;

vector<pair<uint64_t, uint64_t>> read_all_keys(lmdb::env& env, lmdb::dbi& database, unsigned int threads) {
	uint64_t endExclusive;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::cursor cur = lmdb::cursor::open(txn, database);
		std::string_view key;
		if (!cur.get(key, MDB_LAST))
			throw std::runtime_error("couldn't get last key"); //don't have database name to report...
		endExclusive = lmdb::from_sv<uint64_t>(key);
		txn.commit();
	}
	vector<pair<uint64_t, uint64_t>> demanded_interval = {{1, endExclusive}};
	vector<vector<pair<uint64_t, uint64_t>>> tasks = interval_chunk(demanded_interval.begin(), demanded_interval.end(), 20000);

	return transform_reduce(std::move(tasks), threads, [&](vector<pair<uint64_t, uint64_t>> chunk) {
		assert(chunk.size() == 1);
		interval_accumulator<uint64_t> accum(512);
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::cursor cur = lmdb::cursor::open(txn, database);
		std::string_view key = lmdb::to_sv(chunk[0].first);
		if (cur.get(key, MDB_SET_RANGE))
			while (lmdb::from_sv<uint64_t>(key) < chunk[0].second) {
				accum(lmdb::from_sv<uint64_t>(key));
				if (!cur.get(key, MDB_NEXT)) break;
			}
		return std::move(accum).finish();
	},
	//copied from toggles-report.cpp
	[](vector<pair<uint64_t, uint64_t>> left, vector<pair<uint64_t, uint64_t>> right) {
		return interval_union(left.begin(), left.end(), right.begin(), right.end());
	});
}

int repair_completions_mode(std::string_view db_path, const vector<std::string_view>& args) {
	bool list_all = false, rebuild_close = false, rebuild_mirror = false, rebuild_connect = false;
	vector<std::string_view> rebuild_combine;
	unsigned int num_threads = 1;
	vector<std::string_view> dump, compact;
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--list"sv)
			list_all = true;
		else if (args[i] == "--dump"sv)
			dump.push_back(args[++i]);
		else if (args[i] == "--compact"sv)
			compact.push_back(args[++i]);
		else if (args[i] == "--rebuild-close"sv)
			rebuild_close = true;
		else if (args[i] == "--rebuild-mirror"sv)
			rebuild_mirror = true;
		else if (args[i] == "--rebuild-connect"sv)
			rebuild_connect = true;
		else if (args[i] == "--rebuild-combine"sv)
			dump.push_back(args[++i]);
		else if (args[i] == "--threads"sv)
			num_threads = from_string<unsigned int>(args[++i]);
		else {
			fmt::print(stderr, "ERROR: unknown option {}\n", args[i]);
			std::exit(2);
		}
	}

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);

	lmdb::dbi gadget_hashtable, gadget_index, completions, predicates;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		predicates = lmdb::dbi::open(txn, "predicates");
		txn.commit();
	}

	auto open_database = [&env](std::string_view name) {
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::dbi database = lmdb::dbi::open(txn, std::string(name).c_str());
		txn.commit();
		return database;
	};

	if (list_all) {
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::cursor cur = lmdb::cursor::open(txn, completions);
		std::string_view key, value;
		for (auto cur_op = MDB_FIRST; cur.get(key, value, cur_op); cur_op = MDB_NEXT) {
			const pair<uint64_t, uint64_t>* first = reinterpret_cast<const pair<uint64_t, uint64_t>*>(value.data());
			const pair<uint64_t, uint64_t>* last = first + value.size() / sizeof(pair<uint64_t, uint64_t>);
			std::size_t ids = interval_size(first, last), intervals = value.size() / sizeof(pair<uint64_t, uint64_t>);
			fmt::print("{}: {} ids, {} intervals, {} KiB\n", key, ids, intervals, value.size() / 1024);
		}
		txn.commit();
	}

	for (std::string_view key : dump) {
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		std::string_view value;
		if (!completions.get(txn, key, value)) {
			fmt::print("can't dump nonexistent {}\n", key);
			continue;
		}
		auto remainder = value.size() % sizeof(pair<uint64_t, uint64_t>);
		if (remainder)
			fmt::print(stderr, "warning: {} has bad length {} (remainder {})\n", key, value.size(), remainder);
		fmt::print("{}: ", key);
		std::size_t max_gadget = get_current_max_gadget_id(txn, gadget_index) + 1;
		std::size_t idx = 0;
		pair<uint64_t, uint64_t> last = {0, 0};
		while (value.size() >= sizeof(pair<uint64_t, uint64_t>)) {
			pair<uint64_t, uint64_t> p;
			std::memcpy(&p, value.data(), sizeof(p));
			fmt::print("{}, ", p);
			value.remove_prefix(sizeof(p));

			if (!(p.first < p.second) || p.first == 0 || p.second == 0)
				fmt::print(stderr, "warning: {} interval {} is bad: {}\n", key, idx, p);
			if (!(p.first < max_gadget) || !(p.second <= max_gadget))
				fmt::print(stderr, "warning: {} interval {} is too large: {} {}\n", key, idx, max_gadget, p);
			if (!(last.second < p.first))
				fmt::print(stderr, "warning: {} interval {} missorted: {}, {}\n", key, idx, last, p);
			last = p;
		}
		fmt::print("\n");
		txn.commit();
	}

	for (std::string_view key : compact) {
		Stopwatch stopwatch = Stopwatch::process();
		lmdb::txn txn = lmdb::txn::begin(env);
		compact_completion(txn, completions, key);
		txn.commit();
		fmt::print("compacted {} in {}\n", key, stopwatch.elapsed().hms());
	}

	if (rebuild_close)
		//TODO: for close repair, we also need to read the edge's output, so
		//we'd use visit_edges<SimpleEdge>.
		throw std::logic_error("TODO implement rebuilding close");

	if (rebuild_mirror) {
		Stopwatch keys_watch = Stopwatch::process();
		lmdb::dbi mirror_edges = open_database("edges-mirror");
		vector<pair<uint64_t, uint64_t>> result = read_all_keys(env, mirror_edges, num_threads);
		Stopwatch::Result keys_elapsed = keys_watch.elapsed();
		fmt::print("read mirror keys in {} ({:.2f})\n", keys_elapsed.hms(), keys_elapsed.utilization());

		lmdb::txn txn = lmdb::txn::begin(env); //write txn
		std::string_view value(reinterpret_cast<const char*>(result.data()),
			result.size() * sizeof(pair<uint64_t, uint64_t>));
		if (!completions.put(txn, "mirror"sv, value))
			throw std::runtime_error("failed to write mirror completions");
		txn.commit();
		fmt::print("wrote mirror completion\n");
	}

	if (rebuild_connect) {
		Stopwatch keys_watch = Stopwatch::process();
		lmdb::dbi connect_edges = open_database("edges-connect");
		vector<pair<uint64_t, uint64_t>> result = read_all_keys(env, connect_edges, num_threads);
		Stopwatch::Result keys_elapsed = keys_watch.elapsed();
		fmt::print("read connect keys in {} ({:.2f})\n", keys_elapsed.hms(), keys_elapsed.utilization());

		Stopwatch pred_watch = Stopwatch::process();
		//We are also complete for any gadget with state size <= 3.
		if (update_predicates(env, predicates, gadget_hashtable, gadget_index,
				std::numeric_limits<uint64_t>::max(), num_threads)) {
			Stopwatch::Result elapsed = pred_watch.elapsed();
			fmt::print("updated SL predicates in {} ({:.2f})\n", elapsed.hms(), elapsed.utilization());
		}
		vector<pair<uint64_t, uint64_t>> too_small = get_predicate(env, predicates, PredicateKind::locations, 3);
		result = interval_union(result.begin(), result.end(), too_small.begin(), too_small.end());

		lmdb::txn txn = lmdb::txn::begin(env); //write txn
		std::string_view value(reinterpret_cast<const char*>(result.data()),
			result.size() * sizeof(pair<uint64_t, uint64_t>));
		if (!completions.put(txn, "connect"sv, value))
			throw std::runtime_error("failed to write connect completions");
		txn.commit();
		fmt::print("wrote connect completion\n");
	}

	for (std::string_view text_number : rebuild_combine) {
		Stopwatch keys_watch = Stopwatch::process();
		std::string completions_key = fmt::format("combine-{}", text_number);
		lmdb::dbi database = open_database("edges-"+completions_key);
		vector<pair<uint64_t, uint64_t>> result = read_all_keys(env, database, num_threads);
		Stopwatch::Result keys_elapsed = keys_watch.elapsed();
		fmt::print("read {} keys in {} ({:.2f})\n", completions_key, keys_elapsed.hms(), keys_elapsed.utilization());

		lmdb::txn txn = lmdb::txn::begin(env); //write txn
		std::string_view value(reinterpret_cast<const char*>(result.data()),
			result.size() * sizeof(pair<uint64_t, uint64_t>));
		if (!completions.put(txn, completions_key, value))
			throw std::runtime_error(fmt::format("failed to write {} completions", completions_key));
		txn.commit();
		fmt::print("wrote {} completion\n", completions_key);
	}

	return 0;
}