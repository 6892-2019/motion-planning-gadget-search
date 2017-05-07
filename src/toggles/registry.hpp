#ifndef TOGGLES_REGISTRY_HPP
#define TOGGLES_REGISTRY_HPP

#include "../automaton.hpp"
#include "canonicalize.hpp"

using automaton::Automaton;
using automaton::AutomatonBase;
using automaton::StateSet;
using automaton::SymbolSet;
using location_type = std::uint8_t;
using automaton_type = automaton::Automaton<8>;
//TODO: try unique_ptr here
using automaton_const_ptr = std::shared_ptr<const automaton_type>;

struct Gadget {
	Gadget() = default;
	Gadget(const automaton_type& a, unsigned int locations) : Gadget(automaton_type(a), locations) {}
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
		auto ls = left.a_->state_size(), rs = right.a_->state_size();
//		int ls = std::abs(21 - (signed int)left.a_->size()), rs = std::abs(21 - (signed int)right.a_->size());
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

automaton_type mirror(const automaton_type& a, unsigned int locations);

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
		//TODO: for max-size gadgets, we need to record that we've seen them
		//(and don't generate them again) but there's no reason to iterate over
		//them when trying to combine, because they aren't combinable
		//We do want them to have a parent number, because they could be a
		//connect-parent of a new gadget
		//If we add a 'delete location' ("dead-end") operation, though, they are
		//valid targets.
		graphs_[index] = (std::move(queue_.back().first));
		provenance_[index] = queue_.back().second;
		queue_.pop_back();
		MAYBE_UNUSED auto erased = waiting_.erase(graphs_[index].a_.get());
		assert(erased && "dequeued, but couldn't erase from waiting set");

		//TODO: doing this now saves some memory not holding it in the queue
		//(and some time not computing it for gadgets not dequeued) but puts it
		//in the serial fraction.
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
	bool offer(Gadget g, Provenance p, std::size_t automatonHash) {
		//Empty automata can't be usefully combined, so no reason to store them.
		//TODO: isEmpty() isn't const, so we can't call it here.
		//TODO: we know/assume the automata are minimal here, no reason to iterate
		if (g.a_->edge_size() == 0) return false;
		//Require a full set of active locations.
		assert(g.a_->activeAlphabet().size() == g.locations_);

		std::size_t probe = automatonHash % closed_.size();
		while (closed_[probe] != ABSENT) {
			if (g == graphs_[closed_[probe]])
				return false;
			//TODO: fail out if completely full?
			++probe;
			if (probe == closed_.size())
				probe = 0;
		}
		//TODO: probably check waiting_ first (it's usually larger)
		//TODO: this will hash it again, sigh
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

using Result = std::vector<std::tuple<Gadget, Provenance, std::size_t>>;
Gadget known_gadget(const std::string& name);

#endif /* TOGGLES_REGISTRY_HPP */

