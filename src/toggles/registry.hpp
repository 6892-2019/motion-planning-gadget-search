#ifndef TOGGLES_REGISTRY_HPP
#define TOGGLES_REGISTRY_HPP

#include "../automaton.hpp"
#include "../packedautomaton.hpp"
#include "canonicalize.hpp"

using std::unique_ptr;
using automaton::Automaton;
using automaton::AutomatonBase;
using automaton::PackedAutomaton;
using automaton::StateSet;
using automaton::SymbolSet;
using location_type = std::uint8_t;
using automaton_type = automaton::Automaton<8>;

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

	bool isInput() const {return first == ALLONES;}
private:
	static constexpr std::uint32_t ALLONES = std::numeric_limits<std::uint32_t>::max();
	static constexpr std::uint32_t TOPBIT = 1 << 31;
};

automaton_type mirror(const automaton_type& a, unsigned int locations = a.active_alphabet_size());

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
//		waiting_.set_empty_key(empty_.get());
		waiting_.set_deleted_key(deleted_.get());
	}

	/**
	 * Registers the graph at the head of the queue.
	 * @return a non-owning pointer to the registered graph
	 */
	const PackedAutomaton* register_next() {
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
	bool offer(unique_ptr<const PackedAutomaton> a, Provenance p, std::size_t automatonHash) {
		//Empty automata can't be usefully combined, so no reason to store them.
		//TODO: isEmpty() isn't const, so we can't call it here.
		//TODO: we know/assume the automata are minimal here, no reason to iterate
		if (a->edge_size() == 0) return false;
		//TODO: we could compute the hash in the worker thread instead if there
		//were a way to provide it to the hash table
		if (closed_.count(a)) return false;

		const PackedAutomaton* nonowning = a.get();
		MAYBE_UNUSED auto insertres = closed_.insert(std::move(a));
		assert(insertres.second);
		queue_.emplace_back(nonowning, p);
		std::push_heap(queue_.begin(), queue_.end(), queue_order);
		return true;
	}

	index_type registered_size() const {
		return size_;
	}
	const Provenance& provenance(index_type i) const {
		return provenance_[i];
	}
	std::size_t waiting_size() const {
		return queue_.size();
	}
private:
	static constexpr double LOAD_FACTOR = 0.8;
	static constexpr index_type ABSENT = std::numeric_limits<index_type>::max();

	static bool queue_order(const std::pair<const PackedAutomaton*, Provenance>& left, const std::pair<const PackedAutomaton*, Provenance>& right) {
		//std::*_heap works with max-heaps, grumble
		return left.first->state_size() > right.first->state_size();
	}

	std::uint32_t size_;
	dynarray<Provenance> provenance_;
	//non-owning pointer to the automata held by the closed set's unique_ptr
	std::vector<std::pair<const PackedAutomaton*, Provenance>> queue_;
	google::sparse_hash_set<unique_ptr<const AutomatonBase>, indirect_hash, indirect_equal> closed_;
};

using Result = std::vector<std::tuple<Gadget, Provenance, std::size_t>>;
Gadget known_gadget(const std::string& name);

#endif /* TOGGLES_REGISTRY_HPP */

