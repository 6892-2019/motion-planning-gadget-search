#include "precompiled.hpp"
#include "../predicates.hpp"
#include "intervals.hpp"
#include "stringutils.hpp"
#include "stopwatch.hpp"

using std::vector;
using std::uint64_t;
using std::pair;
using namespace std::literals::string_view_literals;

//defined in sync.hpp
void initialize_predicates_database(lmdb::txn& txn, lmdb::dbi& predicates);

int update_predicates_mode(std::string_view db_path, const vector<std::string_view>& args) {
	bool list_all_predicates = false, reinitialize = false;
	uint64_t update = 0;
	unsigned int num_threads = 1;
	vector<pair<unsigned int, PredicateKind>> to_create, to_delete;
	vector<std::string_view> dump;
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--update"sv)
			if (i+1 < args.size() && args[i+1][0] != '-')
				update = from_string<uint64_t>(args[++i]);
			else
				update = std::numeric_limits<uint64_t>::max();
		else if (args[i] == "--list"sv)
			list_all_predicates = true;
		else if (args[i] == "--dump"sv)
			dump.push_back(args[++i]);
		else if (args[i] == "--reinitialize"sv)
			reinitialize = true;
		else if (args[i].find("--delete-") == 0) {//ersatz starts_with
			to_delete.emplace_back(to_uint(args[i+1]), predicate_kind_from_string(args[i].substr(9)));
			++i;
		} else if (args[i].find("--create-") == 0) {
			to_create.emplace_back(to_uint(args[i+1]), predicate_kind_from_string(args[i].substr(9)));
			++i;
		} else if (args[i] == "--threads"sv)
			num_threads = from_string<unsigned int>(args[++i]);
		else {
			fmt::print(stderr, "ERROR: unknown option {}\n", args[i]);
			std::exit(2);
		}
	}

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(10UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);

	lmdb::dbi gadget_hashtable, gadget_index, predicates;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		predicates = lmdb::dbi::open(txn, "predicates");
		txn.commit();
	}

	if (list_all_predicates) {
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::cursor cur = lmdb::cursor::open(txn, predicates);
		std::string_view key, value;
		for (auto cur_op = MDB_FIRST; cur.get(key, value, cur_op); cur_op = MDB_NEXT) {
			if (key == "valid_before"sv)
				fmt::print("valid_before: {}\n", lmdb::from_sv<uint64_t>(value));
			else {
				const pair<uint64_t, uint64_t>* first = reinterpret_cast<const pair<uint64_t, uint64_t>*>(value.data());
				const pair<uint64_t, uint64_t>* last = first + value.size() / sizeof(pair<uint64_t, uint64_t>);
				std::size_t ids = interval_size(first, last), intervals = value.size() / sizeof(pair<uint64_t, uint64_t>);
				fmt::print("{}: {} ids, {} intervals, {} KiB\n", key, ids, intervals, value.size() / 1024);
			}
		}
		txn.commit();
	}

	if (reinitialize) {
		lmdb::txn txn = lmdb::txn::begin(env);
		initialize_predicates_database(txn, predicates);
		txn.commit();
		fmt::print("reinitialized predicates database\n");
	}

	for (std::string_view key : dump) {
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		std::string_view value;
		if (!predicates.get(txn, "valid_before", value))
			throw std::logic_error("no valid_before?");
		uint64_t valid_before = lmdb::from_sv<uint64_t>(value);
		if (!predicates.get(txn, key, value)) {
			fmt::print("can't dump nonexistent {}\n", key);
			continue;
		}
		auto remainder = value.size() % sizeof(pair<uint64_t, uint64_t>);
		if (remainder)
			fmt::print(stderr, "warning: {} has bad length {} (remainder {})\n", key, value.size(), remainder);
		fmt::print("{}: ", key);
		std::size_t idx = 0;
		pair<uint64_t, uint64_t> last = {0, 0};
		while (value.size() >= sizeof(pair<uint64_t, uint64_t>)) {
			pair<uint64_t, uint64_t> p;
			std::memcpy(&p.first, value.data(), sizeof(p.first));
			std::memcpy(&p.second, value.data()+sizeof(p.first), sizeof(p.second));
			fmt::print("{}, ", p);
			value.remove_prefix(sizeof(p));

			if (!(p.first < p.second) || p.first == 0 || p.second == 0)
				fmt::print(stderr, "warning: {} interval {} is bad: {}\n", key, idx, p);
			if (!(p.first < valid_before) || !(p.second <= valid_before))
				fmt::print(stderr, "warning: {} interval {} is too large: {} {}\n", key, idx, valid_before, p);
			if (!(last.second < p.first))
				fmt::print(stderr, "warning: {} interval {} missorted: {}, {}\n", key, idx, last, p);
			last = p;
		}
		fmt::print("\n");
		txn.commit();
	}

	for (auto p : to_delete) {
		lmdb::txn txn = lmdb::txn::begin(env);
		if (!predicates.del(txn, fmt::format("{}<={}", p.second, p.first)))
			fmt::print("{}<={} not deleted because it does not exist\n", p.second, p.first);
		txn.commit();
		fmt::print("deleted {}<={}\n", p.second, p.first);
	}

	if (!to_create.empty()) {
		Stopwatch stopwatch = Stopwatch::process();
		if (create_predicates(env, predicates, gadget_hashtable, gadget_index, to_create, num_threads))
			fmt::print("created predicates in {}\n", stopwatch.elapsed().hms());
		else
			fmt::print("skipped creating predicates (nothing to do)\n");
	}

	if (update) {
		Stopwatch stopwatch = Stopwatch::process();
		//maybe should return the number of gadgets added to the predicates for reporting?
		if (update_predicates(env, predicates, gadget_hashtable, gadget_index, update, num_threads))
			fmt::print("updated predicates in {}\n", stopwatch.elapsed().hms());
		else
			fmt::print("predicates already up-to-date\n");
	}
	return 0;
}