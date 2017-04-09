#include "precompiled.hpp"
#include "../automaton.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"

#include <nausparse.h>

using location_type = std::uint8_t;
using automaton_type = automaton::Automaton<8>;
using automaton_ptr = typename automaton_type::ptr;
using automaton_const_ptr = typename automaton_type::const_ptr;
using alphabet_type = ByteAlphabet<8>;
using regex_type = automaton::Regex<alphabet_type>;

struct Gadget {
	Gadget() = default;
	Gadget(automaton_const_ptr a, unsigned int locations) : a_(a), locations_(locations) {}

	bool operator==(const Gadget& other) const {
		return *a_ == *other.a_;
	}
	bool operator!=(const Gadget& other) const {
		return !(*this == other);
	}

	static bool larger_than(const Gadget& left, const Gadget& right) {
		//TODO: we could also sort by transitions or edges, but that's linear
		//in the size of the automaton.  We're manually tracking the effective
		//alphabet size (locations_) to avoid a similar linear scan.
		std::size_t ls = left.a_->size(), rs = right.a_->size();
		return std::tie(left.locations_, ls) > std::tie(right.locations_, rs);
	}

	//TODO: const_ptr?  we shouldn't need to ever modify it
	automaton_const_ptr a_;
	unsigned int locations_;

	friend class std::hash<Gadget>;
};

namespace std {
template<>
struct hash<Gadget> {
	size_t operator()(const Gadget& g) const {
		return std::hash<automaton_type>()(*g.a_);
	}
};
}

struct Provenance {
	std::uint32_t first, second;
	location_type i, j;
	Provenance() = default;
	Provenance(std::uint32_t parent, location_type connection)
		: first(parent), second(std::numeric_limits<std::uint32_t>::max()),
		i(connection), j(std::numeric_limits<location_type>::max()) {}
	Provenance(std::uint32_t firstParent, location_type firstSplice,
			std::uint32_t secondParent, location_type secondSplice)
		: first(firstParent), second(secondParent), i(firstSplice), j(secondSplice) {}
};

class Registry {
public:
	using index_type = std::uint32_t;
	Registry(std::size_t maxSize) : size_(0), graphs_(maxSize), provenance_(maxSize),
			closed_((std::size_t)(((double)maxSize) * (1.0/LOAD_FACTOR))), queue_(), waiting_() {
		//TODO: guess at and reserve queue_ and waiting_
		std::fill(closed_.begin(), closed_.end(), ABSENT);
	}

	/**
	 * Registers the graph at the head of the queue, returning its index.
	 * @return the index of the queued graph
	 */
	index_type register_next() {
		std::pop_heap(queue_.begin(), queue_.end(), queue_order);
		index_type index = size_;
		++size_;
		graphs_[index] = (std::move(queue_.back().first));
		provenance_[index] = queue_.back().second;
		queue_.pop_back();
		MAYBE_UNUSED auto erased = waiting_.erase(graphs_[index]);
		assert(erased && "dequeued, but couldn't erase from waiting set");

		std::size_t hash = std::hash<Gadget>()(graphs_[index]);
		std::size_t probe = hash % closed_.size();
		while (closed_[probe] != ABSENT) {
			//TODO: fail out if completely full?
			++probe;
			if (probe == closed_.size())
				probe = 0;
		}
		closed_[probe] = index;
		return index;
	}

	/**
	 * Offers a graph for queuing, which will be added iff it has not been
	 * registered nor is waiting (already queued).
	 * @return true iff the graph was queued
	 */
	bool offer(Gadget g, Provenance p) {
		//Empty automata can't be usefully combined, so no reason to store them.
		//TODO: isEmpty() isn't const, so we can't call it here.
		if (g.a_->numTransitions() == 0) return false;

		std::size_t hash = std::hash<Gadget>()(g);
		std::size_t probe = hash % closed_.size();
		while (closed_[probe] != ABSENT) {
			if (g == graphs_[closed_[probe]])
				return false;
			//TODO: fail out if completely full?
			++probe;
			if (probe == closed_.size())
				probe = 0;
		}
		if (waiting_.count(g)) return false;

		waiting_.insert(g);
		queue_.emplace_back(std::move(g), p);
		std::push_heap(queue_.begin(), queue_.end(), queue_order);
		return true;
	}

	index_type size() const {
		return size_;
	}
	const Gadget& at(index_type i) const {
		return graphs_[i];
	}
private:
	static constexpr double LOAD_FACTOR = 0.8;
	static constexpr index_type ABSENT = std::numeric_limits<index_type>::max();

	static bool queue_order(const std::pair<Gadget, Provenance>& left, const std::pair<Gadget, Provenance>& right) {
		//std::*_heap works with max-heaps, grumble
		return Gadget::larger_than(left.first, right.first);
	}

	std::uint32_t size_;
	dynarray<Gadget> graphs_;
	dynarray<Provenance> provenance_;
	dynarray<std::uint32_t> closed_;
	std::vector<std::pair<Gadget, Provenance>> queue_;
	//so long as Gadget is a handle type, this is fine
	std::unordered_set<Gadget> waiting_;
};
constexpr Registry::index_type Registry::ABSENT;

static Registry registry(9001);

void canonicalize(automaton_ptr a, unsigned int locations) {
	sparsegraph sg, canon;
	SG_INIT(sg);
	sg.nv = 0;
	sg.nde = 0;
	SG_INIT(canon);
	canon.nv = 0;
	canon.nde = 0;

	using state_type = automaton_type::state_type;
	using symbol_type = automaton_type::symbol_type;
	//TODO: these could just be functions
	std::unordered_map<std::pair<state_type, unsigned int>, int,
		boost::hash<std::pair<state_type, unsigned int>>> towers;
	std::unordered_map<unsigned int, int> edgecolors;
	for (state_type s = 0; s < a->size(); ++s)
		for (unsigned int l = 0; l < locations; ++l)
			towers[{s, l}] = sg.nv++;
	for (unsigned int l = 0; l < locations; ++l)
		edgecolors[l] = sg.nv++;
	sg.nde = 2 * towers.size() //undirected cycle through each tower
			+ 2 * edgecolors.size() //undirected cycle through the colors
			+ 2 * a->size() * edgecolors.size() //undirected edge between each color and each node in its level
			+ a->edges();
	SG_ALLOC(sg, sg.nv, sg.nde, "asdf");

	auto incr = [&](symbol_type s){return s == locations-1 ? 0 : s+1;};
	auto decr = [&](symbol_type s){return s == 0 ? locations-1 : s-1;};

	int ei = 0;
	for (state_type s = 0; s < a->size(); ++s)
		for (unsigned int l = 0; l < locations; ++l) {
			int vi = towers.at({s, l});
			sg.v[vi] = ei;

			//below/above in the tower
			sg.e[ei++] = l == 0 ? towers[{s, locations-1}] : towers[{s, l-1}];
			sg.e[ei++] = l == locations-1 ? towers[{s, 0}] : towers[{s, l+1}];

			sg.e[ei++] = edgecolors.at(l);

			auto dests = a->step(s, l);
			assert(dests.size() <= 1 && "should already be deterministic");
			if (!dests.empty())
				sg.e[ei++] = towers.at({dests.front(), l});

			sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
		}

	for (unsigned int l = 0; l < locations; ++l) {
		int vi = edgecolors.at(l);
		sg.v[vi] = ei;
		sg.e[ei++] = l == 0 ? edgecolors.at(locations-1) : edgecolors.at(l-1);
		sg.e[ei++] = l == locations-1 ? edgecolors.at(0) : edgecolors.at(l+1);
		for (state_type s = 0; s < a->size(); ++s)
			sg.e[ei++] = towers.at({s, l});
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}
	assert(ei == sg.nde && "wrong number of edges");

	dynarray<int> lab(sg.nv), ptn(sg.nv);
	std::iota(lab.begin(), lab.end(), 0);
	std::fill(ptn.begin(), ptn.end(), 1); //counter-intuitively, partitions end at 0
	ptn[towers.size()-1] = 0;
	ptn[ptn.size()-1] = 0;

	DEFAULTOPTIONS_SPARSEDIGRAPH(options);
	options.getcanon = TRUE;
	options.defaultptn = FALSE;
	statsblk stats;
	dynarray<int> orbits(sg.nv);
	sparsenauty(&sg, lab.begin(), ptn.begin(), orbits.begin(), &options, &stats, &canon);
	//we don't actually need the graphs at all, just the labeling
	SG_FREE(canon);
	SG_FREE(sg);

	dynarray<state_type> stateinvperm(a->size());
	std::iota(stateinvperm.begin(), stateinvperm.end(), 0);
	std::sort(stateinvperm.begin(), stateinvperm.end(), [&](auto l, auto r){return lab[towers.at({l, 0})] < lab[towers.at({r, 0})];});
	dynarray<state_type> stateperm(a->size());
	for (state_type i = 0; i < stateperm.size(); ++i)
		stateperm[stateinvperm[i]] = i;
	//state 0 is the initial state, we can't renumber it
	std::iter_swap(stateperm.begin(), std::find(stateperm.begin(), stateperm.end(), 0));

	//Locations are intrinsically ordered; we use the labeling to select a
	//start point and a direction, then we walk the cycle ourselves.
	symbol_type minloc = *std::min_element(boost::counting_iterator<symbol_type>(0),
		boost::counting_iterator<symbol_type>(locations),
		[&](auto l, auto r){return lab[edgecolors.at(l)] < lab[edgecolors.at(r)];});
	dynarray<automaton_type::symbol_type> locationperm(automaton_type::alphabet_size);
	bool cycleDown = lab[edgecolors.at(decr(minloc))] < lab[edgecolors.at(incr(minloc))];
	for (unsigned int i = 0; i < locations; ++i, minloc = cycleDown ? decr(minloc) : incr(minloc))
		locationperm[i] = minloc;
	std::fill(locationperm.begin()+locations, locationperm.end(), std::numeric_limits<automaton_type::symbol_type>::max());

	a->renumber(stateperm.begin(), locationperm.begin());
	a->prepareForEquals();
}

template<typename OutputIterator>
OutputIterator combine(Registry::index_type l, Registry::index_type r, OutputIterator out) {
	const Gadget& left = registry.at(l), &right = registry.at(r);
	if (left.locations_ + right.locations_ > automaton_type::alphabet_size) {
		std::cout << "Skipping combine due to size\n";
		return out;
	}

	using state_type = automaton_type::state_type;
	std::vector<automaton_type::symbol_type> slide(automaton_type::alphabet_size);
	std::vector<automaton_type::symbol_type> sliderotate(automaton_type::alphabet_size);
	std::fill(slide.begin(), slide.begin()+right.locations_, std::numeric_limits<automaton_type::symbol_type>::max());
	std::iota(slide.begin()+right.locations_, slide.begin()+right.locations_+left.locations_, 0);
	std::fill(slide.begin()+right.locations_+left.locations_, slide.end(), std::numeric_limits<automaton_type::symbol_type>::max());
	for (location_type ll = 0; ll < left.locations_; ++ll) {
		std::fill(sliderotate.begin(), sliderotate.end(), std::numeric_limits<automaton_type::symbol_type>::max());
		std::iota(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_, 0);
		for (location_type rl = 0; rl < right.locations_; ++rl) {
			automaton_ptr lm = left.a_->clone();
			lm->renumberAlphabet(slide);
			automaton_ptr rm = right.a_->clone();
			rm->renumberAlphabet(sliderotate);
			automaton_ptr combined = automaton_type::shuffleAccept(lm, rm);
			combined->minimize();
			canonicalize(combined, left.locations_ + right.locations_);
			*out++ = std::make_pair(Gadget(combined, left.locations_ + right.locations_),
					Provenance(l, ll, r, rl));

			std::rotate(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_-1, sliderotate.begin()+ll+right.locations_);
		}
		std::swap(slide[ll], slide[ll+right.locations_]);
	}

	return out;
}

template<typename OutputIterator>
OutputIterator connect(Registry::index_type gadgetIndex, OutputIterator out) {
	const Gadget& g = registry.at(gadgetIndex);
	if (g.locations_ <= 3) {
		std::cout << "Skipping connect due to size (" << g.locations_ <<")\n";
		return out;
	}

	using state_type = automaton_type::state_type;
	using symbol_type = automaton_type::symbol_type;
	std::vector<automaton_type::symbol_type> alphamap(automaton_type::alphabet_size);
	for (unsigned int l = 0; l < g.locations_; ++l) {
		unsigned int m = (l+1) % g.locations_;
		automaton_ptr connected = g.a_->clone();
		//TODO: fixpoint iteration may not actually be necessary
		bool progress = true;
		while (progress) {
			progress = false;
			for (state_type s = 0; s < connected->size(); ++s) {
				if (connected->accepts(s)) continue;
				auto dests = connected->step(s, l);
				for (state_type d : dests) {
					assert(connected->accepts(d));
					for (state_type e : connected->step(d, m))
						progress |= connected->addEpsilon(s, e);
				}

				dests = connected->step(s, m);
				for (state_type d : dests) {
					assert(connected->accepts(d));
					for (state_type e : connected->step(d, l))
						progress |= connected->addEpsilon(s, e);
				}
			}

			//Transitive closure.
			//TODO: move to Automaton? (minus only being on non-accept states)
			//If we renumbered l to m, transitive-closed, then deleted m, that would be enough (?).
			for (state_type s = 0; s < connected->size(); ++s) {
				if (connected->accepts(s)) continue;
				for (symbol_type a = 0; a < g.locations_; ++a) {
					for (state_type d : connected->step(s, a)) {
						assert(connected->accepts(d));
						for (state_type e : connected->step(d, a))
							progress |= connected->addEpsilon(s, e);
					}
				}
			}
		}

		std::iota(alphamap.begin(), alphamap.begin()+g.locations_, 0);
		std::fill(alphamap.begin()+g.locations_, alphamap.end(), std::numeric_limits<symbol_type>::max());
		//remove larger first to avoid off-by-one
		alphamap.erase(alphamap.begin()+std::max(l, m));
		alphamap.erase(alphamap.begin()+std::min(l, m));
		//pad with 0
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		connected->renumberAlphabet(0, connected->size(), alphamap.begin());
		connected->minimize();
		canonicalize(connected, g.locations_ - 2);
		*out++ = std::make_pair(Gadget(connected, g.locations_ - 2), Provenance(gadgetIndex, l));
	}
	return out;
}

void mainloop(const automaton_type& target) {
	Registry::index_type i = registry.register_next();
	std::vector<std::pair<Gadget, Provenance>> successors;
	for (Registry::index_type j = 0; j <= i; ++j)
		combine(i, j, std::back_inserter(successors));
	connect(i, std::back_inserter(successors));
	unsigned int total = 0;
	for (std::pair<Gadget, Provenance> p : successors) {
		bool offered = registry.offer(p.first, p.second);
		total += offered;
		//only check equality if we offered a new graph
		if (offered && *p.first.a_ == target) {
			std::cout << "found! " << p.second.first << " " << p.second.second << std::endl;
			std::exit(0);
		}
	}
	std::cout << "gadget " << i << " " << registry.at(i).locations_ << " locations, produced " << successors.size() << ", offered " << total << std::endl;
}

int main(int argc, char* argv[]) {
	using R = regex_type;
	automaton_ptr split = R::star(R::alt({R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2)})}),
			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2)})}),
			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1)})})})).compile();
	split->minimize();
	canonicalize(split, 3);
	//TODO: provenance for initial gadgets
	registry.offer(Gadget(std::move(split), 3), Provenance(100000, 0));

	R ltr = R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(3), R::lit(2)})});
	R rtl = R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(2), R::lit(3)})});
	automaton_ptr parallelToggle = R::alt({R::epsilon(), ltr, R::star(R::cat({ltr, rtl})), R::cat({ltr, R::star(R::cat({rtl, ltr}))})}).compile();
	parallelToggle->minimize();
	canonicalize(parallelToggle, 4);
	registry.offer(Gadget(std::move(parallelToggle), 4), Provenance(100001, 0));

	ltr = R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(2), R::lit(3)})});
	rtl = R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(3), R::lit(2)})});
	automaton_ptr antiparallelToggle = R::alt({R::epsilon(), ltr, R::star(R::cat({ltr, rtl})), R::cat({ltr, R::star(R::cat({rtl, ltr}))})}).compile();
	antiparallelToggle->minimize();
	canonicalize(antiparallelToggle, 4);
	while (true) {
		mainloop(*antiparallelToggle);
	}
	return 0;
}