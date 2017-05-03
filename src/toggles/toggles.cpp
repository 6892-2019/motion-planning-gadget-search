#include "precompiled.hpp"
#include "../automaton.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"
#include "canonicalize.hpp"

using automaton::Automaton;
using automaton::AutomatonBase;
using automaton::StateSet;
using automaton::SymbolSet;
using location_type = std::uint8_t;
using automaton_type = automaton::Automaton<8>;
using automaton_const_ptr = std::shared_ptr<const automaton_type>;
using alphabet_type = ByteAlphabet<8>;
using regex_type = automaton::Regex<alphabet_type>;

struct Gadget {
	Gadget() = default;
	Gadget(automaton_type&& a, unsigned int locations) : Gadget(std::make_shared<const automaton_type>(std::move(a)), locations) {}
	Gadget(automaton_const_ptr a, unsigned int locations) : a_(a), mirror_(nullptr), locations_(locations) {}

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
//		return std::tie(left.locations_, ls) > std::tie(right.locations_, rs);
		return std::tie(ls, left.locations_) > std::tie(rs, right.locations_);
	}

	automaton_const_ptr a_;
	//If this gadget is chiral, this is its mirror image; nullptr otherwise.
	//Chirality is not checked until this gadget is registered.
	automaton_const_ptr mirror_;
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
	std::uint32_t first, second, i, j, generation;
	Provenance() = default;
	Provenance(std::uint32_t initialIndex) : first(ALLONES), second(initialIndex), i(ALLONES), j(ALLONES), generation(0) {}
	Provenance(std::uint32_t parent, std::uint32_t connection, AutomatonBase::state_type newInitialState, std::uint32_t generatio)
		: first(parent), second(ALLONES), i(connection), j(newInitialState), generation(generatio) {}
	Provenance(std::uint32_t firstParent, std::uint32_t firstSplice, bool firstMirrored,
			std::uint32_t secondParent, std::uint32_t secondSplice, bool secondMirrored, std::uint32_t generatio)
		: first(firstParent), second(secondParent),
		  i(firstMirrored ? firstSplice | TOPBIT : firstSplice),
		  j(secondMirrored ? secondSplice | TOPBIT : secondSplice), generation(generatio) {}
private:
	static constexpr std::uint32_t ALLONES = std::numeric_limits<std::uint32_t>::max();
	static constexpr std::uint32_t TOPBIT = 1 << 31;
};

automaton_type mirror(const automaton_type& a, unsigned int locations) {
	//TODO: precompute and reuse for 2..automaton_type::alphabet_size_v
	std::vector<AutomatonBase::symbol_type> symbols(
			boost::make_counting_iterator<AutomatonBase::symbol_type>(0),
			boost::make_counting_iterator<AutomatonBase::symbol_type>(automaton_type::alphabet_size_v));
	std::reverse(symbols.begin(), symbols.begin()+locations);
	automaton_type b = a;
	b.renumberAlphabet(symbols);
	canonicalize(b, locations, false);
	return b;
}

class Registry {
public:
	using index_type = std::uint32_t;
	Registry(std::size_t maxSize) : size_(0), graphs_(maxSize), provenance_(maxSize),
			closed_((std::size_t)(((double)maxSize) * (1.0/LOAD_FACTOR))), queue_(), waiting_(),
			empty_(new automaton_type), deleted_(new automaton_type) {
		//TODO: guess at and reserve queue_ and waiting_
		std::fill(closed_.begin(), closed_.end(), ABSENT);

		//all gadget automata have effective alphabet [0, n), so we can use
		//automata with transitions on 1/2 but not 0 as sentinels
		empty_->addState();
		empty_->addState();
		empty_->addTrans(0, 1, 1);
		empty_->setAccept(1);
		deleted_->addState();
		deleted_->addState();
		deleted_->addTrans(0, 2, 1);
		deleted_->setAccept(1);
		waiting_.set_empty_key(empty_.get());
		waiting_.set_deleted_key(deleted_.get());
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
		MAYBE_UNUSED auto erased = waiting_.erase(graphs_[index].a_.get());
		assert(erased && "dequeued, but couldn't erase from waiting set");

		automaton_type m = mirror(*graphs_[index].a_, graphs_[index].locations_);
		if (m != *graphs_[index].a_)
			graphs_[index].mirror_ = std::make_shared<automaton_type>(std::move(m));

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
		//Require a full set of active locations.
		assert(g.a_->activeAlphabet().size() == g.locations_);

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
		if (waiting_.count(g.a_.get())) return false;

		waiting_.insert(g.a_.get());
		queue_.emplace_back(std::move(g), p);
		std::push_heap(queue_.begin(), queue_.end(), queue_order);
		return true;
	}

	index_type registered_size() const {
		return size_;
	}
	const Gadget& at(index_type i) const {
		return graphs_[i];
	}
	const Provenance& provenance(index_type i) const {
		return provenance_[i];
	}
	std::size_t waiting_size() const {
		return waiting_.size();
	}
private:
	static constexpr double LOAD_FACTOR = 0.8;
	static constexpr index_type ABSENT = std::numeric_limits<index_type>::max();

	static bool queue_order(const std::pair<Gadget, Provenance>& left, const std::pair<Gadget, Provenance>& right) {
		//std::*_heap works with max-heaps, grumble
		return Gadget::larger_than(left.first, right.first);
//		return left.second.generation > right.second.generation ||
//				(left.second.generation == right.second.generation && Gadget::larger_than(left.first, right.first));
	}

	std::uint32_t size_;
	dynarray<Gadget> graphs_;
	dynarray<Provenance> provenance_;
	dynarray<std::uint32_t> closed_;
	std::vector<std::pair<Gadget, Provenance>> queue_;
	//non-owning pointer to the automata managed by Gadget's shared_ptr
	google::dense_hash_set<const automaton_type*, indirect_hash, indirect_equal> waiting_;

	//dummy automata used for waiting_'s empty and deleted keys
	std::shared_ptr<automaton_type> empty_, deleted_;
};
constexpr Registry::index_type Registry::ABSENT;

static Registry registry(9001);

template<typename OutputIterator>
OutputIterator combine(Registry::index_type l, bool leftMirror, Registry::index_type r, bool rightMirror, OutputIterator out) {
	const Gadget& left = registry.at(l), &right = registry.at(r);
	const automaton_type& la = leftMirror ? *left.mirror_ : *left.a_;
	const automaton_type& ra = rightMirror ? *right.mirror_ : *right.a_;

	using symbol_type = automaton_type::symbol_type;
	std::vector<symbol_type> slide(automaton_type::alphabet_size_v);
	std::vector<symbol_type> sliderotate(automaton_type::alphabet_size_v);
	std::fill(slide.begin(), slide.begin()+right.locations_, std::numeric_limits<symbol_type>::max());
	std::iota(slide.begin()+right.locations_, slide.begin()+right.locations_+left.locations_, 0);
	std::fill(slide.begin()+right.locations_+left.locations_, slide.end(), std::numeric_limits<symbol_type>::max());
	for (location_type ll = 0; ll < left.locations_; ++ll) {
		std::fill(sliderotate.begin(), sliderotate.end(), std::numeric_limits<symbol_type>::max());
		std::iota(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_, 0);
		automaton_type lm = la;
		lm.renumberAlphabet(slide);
		for (location_type rl = 0; rl < right.locations_; ++rl) {
			automaton_type rm = ra;
			rm.renumberAlphabet(sliderotate);
			automaton_type combined = automaton::shuffleAccept(lm, rm);
			combined.minimize();
			canonicalize(combined, left.locations_ + right.locations_);
			*out++ = std::make_pair(Gadget(std::make_shared<const automaton_type>(std::move(combined)),
					left.locations_ + right.locations_), Provenance(l, ll, leftMirror, r, rl, rightMirror,
					//TODO: make a reasoned choice for this function
					std::max(registry.provenance(l).generation, registry.provenance(r).generation)+1));

			std::rotate(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_-1, sliderotate.begin()+ll+right.locations_);
		}
		std::swap(slide[ll], slide[ll+right.locations_]);
	}

	return out;
}

template<typename OutputIterator>
OutputIterator combine(Registry::index_type l, Registry::index_type r, OutputIterator out) {
	const Gadget& left = registry.at(l), &right = registry.at(r);
	if (left.locations_ + right.locations_ > automaton_type::alphabet_size_v) {
//		std::cout << "Skipping combine due to size\n";
		return out;
	}

	out = combine(l, false, r, false, out);
	if (left.mirror_)
		out = combine(l, true, r, false, out);
	if (right.mirror_)
		out = combine(l, false, r, true, out);
	if (left.mirror_ && right.mirror_) //TODO: do we need this?
		out = combine(l, true, r, true, out);

	return out;
}

bool acceptingClosure(automaton_type& connected, unsigned int locations) {
	//Transitive closure.
	//TODO: move to Automaton? (minus only being on non-accept states)
	//If we renumbered l to m, transitive-closed, then deleted m, that would be enough (?).
	using state_type = automaton_type::state_type;
	using symbol_type = automaton_type::symbol_type;
	bool progress, changed = false;
	do {
		//TODO: consider a worklist instead of fixpoint iteration
		progress = false;
		for (state_type s = 0; s < connected.size(); ++s) {
			if (connected.accept(s)) continue;
			for (symbol_type a = 0; a < locations; ++a) {
				for (state_type d : connected.step(s, a)) {
					assert(connected.accept(d));
					for (state_type e : connected.step(d, a))
						progress |= connected.addEpsilon(s, e);
				}
			}
		}
		changed |= progress;
	} while (progress);
	return changed;
}

template<typename OutputIterator>
OutputIterator connect(Registry::index_type gadgetIndex, const automaton_type& a, OutputIterator out) {
	const Gadget& g = registry.at(gadgetIndex);
	if (g.locations_ <= 3) {
		std::cout << "Skipping connect due to size (" << g.locations_ <<")\n";
		return out;
	}

	using state_type = automaton_type::state_type;
	using symbol_type = automaton_type::symbol_type;
	auto enjoin = [](automaton_type& a, symbol_type l, symbol_type m) -> bool {
		bool progress, changed = false;
		//TODO: instead of fixpoint iteration, we should put the changed state s
		//on a worklist and iterate until it's empty
		do {
			progress = false;
			for (state_type s = 0; s < a.size(); ++s) {
				if (a.accept(s)) continue;
				auto dests = a.step(s, l);
				for (state_type d : dests) {
					assert(a.accept(d));
					for (state_type e : a.step(d, m))
						progress |= a.addEpsilon(s, e);
				}

				dests = a.step(s, m);
				for (state_type d : dests) {
					assert(a.accept(d));
					for (state_type e : a.step(d, l))
						progress |= a.addEpsilon(s, e);
				}
			}
			changed |= progress;
		} while (progress);
		return changed;
	};

	std::vector<symbol_type> alphamap(automaton_type::alphabet_size_v);
	for (unsigned int l = 0; l < g.locations_; ++l) {
		unsigned int m = (l+1) % g.locations_;
		auto connected = std::make_shared<automaton_type>(a);
		enjoin(*connected, l, m);
		acceptingClosure(*connected, g.locations_);

		std::iota(alphamap.begin(), alphamap.begin()+g.locations_, 0);
		std::fill(alphamap.begin()+g.locations_, alphamap.end(), std::numeric_limits<symbol_type>::max());
		//remove larger first to avoid off-by-one
		alphamap.erase(alphamap.begin()+std::max(l, m));
		alphamap.erase(alphamap.begin()+std::min(l, m));
		//pad with 0
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		connected->renumberAlphabet(0, connected->size(), alphamap.begin());

		//We may have disconnected the automaton (disconnecting the
		//configuration graph of the gadget it represents).  We will swap each
		//accepting state into state 0 instead.
		unsigned int processed = 0;
		const auto accept_size = connected->accept_size();
		for (state_type s = 0; processed < accept_size && s < connected->state_size(); ++s) {
			if (!connected->accept(s)) continue;
			++processed;
			//last one can move, others have to copy
			auto op = processed == accept_size ? std::move(connected) : std::make_shared<automaton_type>(*connected);
			op->swapStateNumbers(0, s);
			//TODO: this repeated minimization is annoying in the case where we
			//didn't disconnect the automaton...
			op->minimize();
			SymbolSet active = op->activeAlphabet();
			if (active.size() <= 1) continue; //there are no interesting 1-symbol automata
			if (active.size() != g.locations_ - 2) {
				//compress the alphabet
				active.sort();
				std::copy(active.begin(), active.end(), alphamap.begin());
				std::fill(alphamap.begin()+active.size(), alphamap.end(), std::numeric_limits<symbol_type>::max());
				op->renumberAlphabet(alphamap.begin());
			}
			canonicalize(*op, static_cast<std::uint32_t>(active.size()));
			*out++ = std::make_pair(Gadget(std::move(op), static_cast<std::uint32_t>(active.size())),
					//TODO: reasoned choice for +1 generation
					Provenance(gadgetIndex, l, s, registry.provenance(gadgetIndex).generation+1));
		}
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

	out = connect(gadgetIndex, *g.a_, out);
	if (g.mirror_)
		out = connect(gadgetIndex, *g.mirror_, out);
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
	std::cout << "gadget " << i << ", gen " << registry.provenance(i).generation << ", "
			<< registry.at(i).locations_ << " locations, "
			<< (registry.at(i).mirror_ ? "chiral, " : "")
			<< "produced " << successors.size()
			<< ", offered " << total << ", "
			<< registry.waiting_size() << " waiting\n";// << std::endl;
}

int main(int argc, char* argv[]) {
	using R = regex_type;
//	automaton_type split = R::star(R::alt({
//			R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2)})}),
//			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2)})}),
//			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1)})})})).compile();
	automaton_type split = R::star(R::alt({
			R::cat({R::lit(0), R::alt({R::lit(0), R::lit(1), R::lit(2)})}),
			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(1), R::lit(2)})}),
			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(2)})})})).compile();
	split.minimize();
	canonicalize(split, 3);
//	std::cout << split << std::endl;
	registry.offer(Gadget(std::move(split), 3), Provenance(0));

//	automaton_ptr split4 = R::star(R::alt({
//			R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(3)})}),
//			R::cat({R::lit(3), R::alt({R::lit(0), R::lit(1), R::lit(2)})})})).compile();
//	automaton_ptr split4 = R::star(R::alt({
//			R::cat({R::lit(0), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(3), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})})})).compile();
//	split4->minimize();
//	canonicalize(split4, 4);
//	std::cout << *split4 << std::endl;
//	while (true) {
//		mainloop(*split4);
//	}

	R noopR = R::star(R::alt({R::cat({R::lit(0), R::lit(0)}), R::cat({R::lit(1), R::lit(1)}), R::cat({R::lit(2), R::lit(2)}), R::cat({R::lit(3), R::lit(3)})}));
	automaton_type noop = noopR.compile();
	R ltr = R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(3), R::lit(2)})});
	R rtl = R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(2), R::lit(3)})});
	automaton_type parallelToggleBase = R::alt({R::epsilon(), ltr, R::star(R::cat({ltr, rtl})), R::cat({ltr, R::star(R::cat({rtl, ltr}))})}).compile();
	automaton_type parallelToggle = shuffleAccept(noop, parallelToggleBase);
	acceptingClosure(parallelToggle, 4);
	parallelToggle.minimize();
	canonicalize(parallelToggle, 4);
	registry.offer(Gadget(std::move(parallelToggle), 4), Provenance(1));

	//TODO: declare well-known gadgets as constants (maybe functions to create them?)
	//and test they do the right thing (may require teaching build script about
	//sub-project tests...)
	ltr = R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(2), R::lit(3)})});
	rtl = R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(3), R::lit(2)})});
	automaton_type antiparallelToggle = shuffleAccept(noop, R::alt({R::epsilon(), ltr, R::star(R::cat({ltr, rtl})), R::cat({ltr, R::star(R::cat({rtl, ltr}))})}).compile());
	acceptingClosure(antiparallelToggle, 4);
	antiparallelToggle.minimize();
	canonicalize(antiparallelToggle, 4);
	while (true) {
		mainloop(antiparallelToggle);
	}
	return 0;
}

//int main(int argc, char* argv[]) {
//	using R = regex_type;
//	automaton_type split = R::star(R::alt({
//		R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2)})}),
//		R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2)})}),
//		R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1)})})})).compile();
//	split.minimize();
//	canonicalize(split, 3);
//	std::cout << split << std::endl;
//	//TODO: provenance for initial gadgets
//	registry.offer(Gadget(std::move(split), 3), Provenance(100000, 0));
//	registry.register_next();
//
//	std::vector<std::pair<Gadget, Provenance>> successors;
//	combine(0, 0, std::back_inserter(successors));
//	bool offered = registry.offer(successors.front().first, successors.front().second);
//	std::cout << offered << std::endl;
//	std::cout << *successors.front().first.a_ << std::endl;
//	successors.clear();
//	registry.register_next();
//
//	connect(1, std::back_inserter(successors));
//	for (auto& p : successors)
//		std::cout << *p.first.a_ << std::endl;
//	std::cout << "ASDFASDF" << std::endl;
//	offered = registry.offer(successors[2].first, successors[2].second);
//	std::cout << offered << std::endl;
//	std::cout << *successors[2].first.a_ << std::endl;
//	successors.clear();
//	registry.register_next();
//
//	connect(2, std::back_inserter(successors));
//	offered = registry.offer(successors.front().first, successors.front().second);
//	std::cout << offered << std::endl;
//	std::cout << *successors.front().first.a_ << std::endl;
//	successors.clear();
//	registry.register_next();
//
//	automaton_type split4 = R::star(R::alt({
//		R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2), R::lit(3)})}),
//		R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2), R::lit(3)})}),
//		R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(3)})}),
//		R::cat({R::lit(3), R::alt({R::lit(0), R::lit(1), R::lit(2)})})})).compile();
//	split4.minimize();
//	canonicalize(split4, 4);
//	std::cout << split4 << std::endl;
//}