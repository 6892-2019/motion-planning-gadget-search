#include "precompiled.hpp"
#include "../gadget-set.hpp"
#include "../anyprov.hpp"
#include "intervals.hpp"
#include "stringutils.hpp"

using std::vector;
using std::uint64_t;
using std::pair;
using namespace std::literals::string_view_literals;

//TODO: somewhat of a duplicate from the reporter
vector<pair<uint64_t, lmdb::dbi>> open_combine_edge_databases(lmdb::env& env, lmdb::txn& txn,
		std::string_view edges_combine_prefix) {
	lmdb::dbi main = lmdb::dbi::open(txn, nullptr);
	lmdb::cursor cur = lmdb::cursor::open(txn, main);
	std::string_view key = edges_combine_prefix;
	if (!cur.get(key, MDB_SET_RANGE)) {
		fmt::print("warning: no combine edge databases found\n");
		return {};
	}

	vector<pair<uint64_t, lmdb::dbi>> ret;
	//no starts_with yet
	while (key.compare(0, edges_combine_prefix.size(), edges_combine_prefix) == 0) {
		std::string_view number = key;
		number.remove_prefix(edges_combine_prefix.size());
		ret.emplace_back(from_string<uint64_t>(number), lmdb::dbi::open(txn, key.data()));
		if (!cur.get(key, MDB_NEXT)) break;
	}
	return ret;
}

int incoming_edges_mode(std::string_view db_path, const vector<std::string_view>& gadget_spec) {
	//TODO: another level of option parsing to allow scanning only a subset of the edge tables
	GadgetSet gadget_set = parse_gid_specs(gadget_spec);

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(10UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);

	lmdb::dbi gadget_hashtable, gadget_index, names_db, connect_edges, connect_skinny_edges, close_edges, mirror_edges;
	vector<pair<uint64_t, lmdb::dbi>> combine_edges, combine_skinny_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		names_db = lmdb::dbi::open(txn, "names");
		connect_edges = lmdb::dbi::open(txn, "edges-connect");
		connect_skinny_edges = lmdb::dbi::open(txn, "edges-skinny-connect");
		close_edges = lmdb::dbi::open(txn, "edges-close");
		mirror_edges = lmdb::dbi::open(txn, "edges-mirror");
		combine_edges = open_combine_edge_databases(env, txn, "edges-combine-"sv);
		combine_skinny_edges = open_combine_edge_databases(env, txn, "edges-skinny-combine-"sv);
		txn.commit();
	}

	vector<uint64_t> all_ids = collect_initial_gadget_set(env, gadget_hashtable, gadget_index, names_db, gadget_set);
	tsl::hopscotch_set<uint64_t, object_hash> targets(all_ids.begin(), all_ids.end());
	std::vector<AnyProv> results;
	std::vector<std::string> skinny_results;
	vector<pair<uint64_t, uint64_t>> every_gadget_ever = {{1, std::numeric_limits<uint64_t>::max()}};
	for (pair<uint64_t, lmdb::dbi>& p : combine_edges)
		visit_edges<CombineEdge>(env, p.second, every_gadget_ever, [input2=p.first, &targets, &results](uint64_t input1, const CombineEdge& e) {
			if (targets.count(e.output))
				results.push_back(AnyProv::combine(input1, input2, e));
			return VisitEdgeResult::proceed;
		});
	visit_edges<ConnectEdge>(env, connect_edges, every_gadget_ever, [&targets, &results](uint64_t input1, const ConnectEdge& e) {
		if (targets.count(e.output))
			results.push_back(AnyProv::connect(input1, e));
		return VisitEdgeResult::proceed;
	});
	visit_edges<SimpleEdge>(env, close_edges, every_gadget_ever, [&targets, &results](uint64_t input1, const SimpleEdge& e) {
		if (targets.count(e.output))
			results.push_back(AnyProv::close(input1, e));
		return VisitEdgeResult::proceed;
	});
	visit_edges<SimpleEdge>(env, mirror_edges, every_gadget_ever, [&targets, &results](uint64_t input1, const SimpleEdge& e) {
		if (targets.count(e.output))
			results.push_back(AnyProv::mirror(input1, e));
		return VisitEdgeResult::proceed;
	});
	visit_skinny_edges(env, connect_skinny_edges, every_gadget_ever, [&targets, &skinny_results](uint64_t input1, uint64_t output) {
		if (targets.count(output))
			skinny_results.push_back(fmt::format("{} = connect {} skinny\n", output, input1));
		return VisitEdgeResult::proceed;
	});
	for (pair<uint64_t, lmdb::dbi>& p : combine_skinny_edges)
		visit_skinny_edges(env, p.second, every_gadget_ever, [input2=p.first, &targets, &skinny_results](uint64_t input1, uint64_t output) {
			if (targets.count(output))
				skinny_results.push_back(fmt::format("{} = combine {} {} skinny\n", output, input1, input2));
			return VisitEdgeResult::proceed;
		});

	std::sort(results.begin(), results.end(), [](const AnyProv& a, const AnyProv& b) {
		//std::tie won't bind to rvalues, grumble
		auto aoutput = a.output(), boutput = b.output();
		auto akind = a.kind(), bkind = b.kind();
		return std::tie(aoutput, akind) < std::tie(boutput, bkind);
	});
	for (const AnyProv& p : results)
		fmt::print("{}\n", p);
	std::sort(skinny_results.begin(), skinny_results.end()); //this sorts numbers wrong, but whatever
	for (const std::string& p : skinny_results)
		fmt::print("{}", p);

	return 0;
}