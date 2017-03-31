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
	bool offer(Gadget&& g, Provenance p) {
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
	//TODO: these could just be functions
	std::unordered_map<std::pair<state_type, location_type>, int,
		boost::hash<std::pair<state_type, location_type>>> towers;
	std::unordered_map<location_type, int> edgecolors;
	for (state_type s = 0; s < a->size(); ++s)
		for (location_type l = 0; l < locations; ++l)
			towers[{s, l}] = sg.nv++;
	for (location_type l = 0; l < locations; ++l)
		edgecolors[l] = sg.nv++;
	sg.nde = 2 * towers.size() //undirected cycle through each tower
			+ 2 * edgecolors.size() //undirected cycle through the colors
			+ 2 * a->size() * edgecolors.size() //undirected edge between each color and each node in its level
			+ a->edges();
	SG_ALLOC(sg, sg.nv, sg.nde, "asdf");

	int ei = 0;
	for (state_type s = 0; s < a->size(); ++s)
		for (location_type l = 0; l < locations; ++l) {
			int vi = towers[{s, l}];
			sg.v[vi] = ei;

			//below/above in the tower
			sg.e[ei++] = l == 0 ? towers[{s, locations-1}] : towers[{s, l-1}];
			sg.e[ei++] = l == locations-1 ? towers[{s, 0}] : towers[{s, l+1}];

			sg.e[ei++] = edgecolors[l];

			auto dests = a->step(s, l);
			assert(dests.size() <= 1 && "should already be deterministic");
			if (!dests.empty())
				sg.e[ei++] = towers[{dests.front(), l}];

			sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
		}

	for (location_type l = 0; l < locations; ++l) {
		int vi = edgecolors[l];
		sg.v[vi] = ei;
		sg.e[ei++] = l == 0 ? edgecolors[static_cast<location_type>(locations-1)] : edgecolors[static_cast<location_type>(l-1)];
		sg.e[ei++] = l == locations-1 ? edgecolors[0] : edgecolors[static_cast<location_type>(l+1)];
		for (state_type s = 0; s < a->size(); ++s)
			sg.e[ei++] = towers[{s, l}];
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}

	dynarray<int> lab(sg.nv), ptn(sg.nv);
	std::iota(lab.begin(), lab.end(), 0);
	std::fill(ptn.begin(), ptn.end(), 1); //counter-intuitively, partitions end at 0
	ptn[towers.size()] = 0;
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

	dynarray<state_type> stateperm(a->size());
	std::iota(stateperm.begin(), stateperm.end(), 0);
	std::sort(stateperm.begin(), stateperm.end(), [&](auto l, auto r){return lab[towers[{l, 0}]] < lab[towers[{r, 0}]];});
	//state 0 is the initial state, we can't renumber it
	std::iter_swap(stateperm.begin(), std::find(stateperm.begin(), stateperm.end(), 0));
	dynarray<automaton_type::symbol_type> locationperm(automaton_type::alphabet_size);
	std::iota(locationperm.begin(), locationperm.begin()+locations, 0);
	std::sort(locationperm.begin(), locationperm.begin()+locations, [&](auto l, auto r){return lab[edgecolors[l]] < lab[edgecolors[r]];});
	std::fill(locationperm.begin()+locations, locationperm.end(), std::numeric_limits<automaton_type::symbol_type>::max());
	//TODO: assert new location numbering is a cycle
	a->renumber(stateperm.begin(), locationperm.begin());
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
	for (location_type ll = 0; ll < left.locations_; ++ll) {
		std::iota(slide.begin(), slide.end(), 0);
		std::fill(sliderotate.begin(), sliderotate.end(), 0);
		std::iota(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_, 0);
		for (location_type rl = 0; rl < right.locations_; ++rl) {
			automaton_ptr combined = left.a_->clone();
			state_type oldsize = combined->size();
			slide.pop_back();
			slide.insert(slide.begin()+ll, std::numeric_limits<automaton_type::symbol_type>::max());
			combined->renumberAlphabet(0, combined->size(), slide);

			combined->append(right.a_);
			std::rotate(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_-1, sliderotate.begin()+ll+right.locations_);
			combined->renumberAlphabet(oldsize, combined->size(), sliderotate);

			for (state_type i = 0; i < oldsize; ++i)
				if (combined->accepts(i))
					for (state_type j = 0; j < oldsize; ++j)
						if (combined->accepts(j))
							combined->addEpsilon(i, j);

			combined->minimize();
			canonicalize(combined, left.locations_ + right.locations_);
			*out++ = std::make_pair(Gadget(combined, left.locations_ + right.locations_),
					Provenance(l, ll, r, rl));
		}
	}

	return out;
}

template<typename OutputIterator>
OutputIterator connect(Registry::index_type gadgetIndex, OutputIterator out) {
	const Gadget& g = registry.at(gadgetIndex);
	if (g.locations_ <= 2) {
		std::cout << "Skipping connect due to size\n";
		return out;
	}

	using state_type = automaton_type::state_type;
	std::vector<automaton_type::symbol_type> alphamap(automaton_type::alphabet_size);
	for (location_type l = 0; l < g.locations_; ++l) {
		location_type m = static_cast<location_type>((l+1) % g.locations_);
		automaton_ptr connected = g.a_->clone();
		//We may need to iterate to a fixpoint to deal with loops?
		for (state_type s = 0; s < connected->size(); ++s) {
			if (connected->accepts(s)) continue;
			auto dests = connected->step(s, l);
			for (state_type d : dests) {
				assert(connected->accepts(d));
				for (state_type e : connected->step(d, m))
					connected->addEpsilon(s, e);
			}

			dests = connected->step(s, m);
			for (state_type d : dests) {
				assert(connected->accepts(d));
				for (state_type e : connected->step(d, m))
					connected->addEpsilon(s, e);
			}
		}

		std::iota(alphamap.begin(), alphamap.end(), 0);
		//remove larger first to avoid off-by-one
		alphamap.erase(alphamap.begin()+std::max(l, m));
		alphamap.erase(alphamap.begin()+std::min(l, m));
		//pad with 0
		alphamap.push_back(std::numeric_limits<automaton_type::symbol_type>::max());
		alphamap.push_back(std::numeric_limits<automaton_type::symbol_type>::max());
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
	for (std::pair<Gadget, Provenance> p : successors) {
		if (*p.first.a_ == target)
			std::cout << "found! " << p.second.first << " " << p.second.second << std::endl;
		registry.offer(std::move(p.first), p.second);
	}
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

	automaton_ptr parallelToggle = R::star(R::cat({R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(3), R::lit(2)})}),
			R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(2), R::lit(3)})})})).compile();
	parallelToggle->minimize();
	canonicalize(parallelToggle, 4);
	registry.offer(Gadget(std::move(parallelToggle), 4), Provenance(100001, 0));

	automaton_ptr antiparallelToggle = R::star(R::cat({R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(2), R::lit(3)})}),
			R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(3), R::lit(2)})})})).compile();
	antiparallelToggle->minimize();
	canonicalize(antiparallelToggle, 4);
	while (true) {
		mainloop(*antiparallelToggle);
	}
	return 0;
}