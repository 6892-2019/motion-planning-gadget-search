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
	Provenance(std::uint32_t parent, std::uint32_t connection, AutomatonBase::state_type newInitialState, bool mirrored, std::uint32_t generatio)
		: first(parent), second(ALLONES), i(mirrored ? connection | TOPBIT : connection), j(newInitialState), generation(generatio) {}
	Provenance(std::uint32_t firstParent, std::uint32_t firstSplice, bool firstMirrored,
			std::uint32_t secondParent, std::uint32_t secondSplice, bool secondMirrored, std::uint32_t generatio)
		: first(firstParent), second(secondParent),
		  i(firstMirrored ? firstSplice | TOPBIT : firstSplice),
		  j(secondMirrored ? secondSplice | TOPBIT : secondSplice), generation(generatio) {}

	bool isInput() const {return first == ALLONES;}
	bool isConnect() const {return second == ALLONES;}
	bool isCombine() const {return !isInput() && !isConnect();}
	boost::container::small_vector<std::uint32_t, 2> parents() {
		boost::container::small_vector<std::uint32_t, 2> p;
		if (first != ALLONES)
			p.push_back(first);
		if (second != ALLONES)
			p.push_back(second);
		return p;
	};
	friend std::ostream& operator<<(std::ostream&, Provenance&);
private:
	static constexpr std::uint32_t ALLONES = std::numeric_limits<std::uint32_t>::max();
	static constexpr std::uint32_t TOPBIT = 1 << 31;
};

automaton_type mirror(const automaton_type& a, unsigned int locations);
inline automaton_type mirror(const automaton_type a) {
	return mirror(a, a.active_alphabet_size());
}

class Registry {
public:
	using index_type = std::uint32_t;
	Registry() : size_(0) {}

	/**
	 * Registers the graph at the head of the queue.
	 * @return a non-owning pointer to the registered graph
	 */
	std::pair<const PackedAutomaton*, index_type> register_next() {
		index_type index = size_++;
		std::pop_heap(queue_.begin(), queue_.end(), queue_order);
		const PackedAutomaton* pack = queue_.back().first;
		provenance_.push_back(queue_.back().second);
		queue_.pop_back();
		return {pack, index};
	}

	/**
	 * Offers a graph for queuing, which will be added iff it has not been
	 * registered nor is waiting (already queued).
	 * @return true iff the graph was queued
	 */
	bool offer(unique_ptr<const PackedAutomaton> a, Provenance p, std::size_t automatonHash = 0) {
		//Empty automata can't be usefully combined, so no reason to store them.
		//TODO: isEmpty() isn't const, so we can't call it here.
		//We know the automata are minimal here; check states, not edges.
		if (a->state_size() <= 1) return false;
		//TODO: we could compute the hash in the worker thread instead if there
		//were a way to provide it to the hash table
		if (closed_.count(a.get())) return false;

		const PackedAutomaton* nonowning = a.get();
		//release(): see comment on closed_ declaration
		MAYBE_UNUSED auto insertres = closed_.insert(a.release());
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
	static bool queue_order(const std::pair<const PackedAutomaton*, Provenance>& left, const std::pair<const PackedAutomaton*, Provenance>& right) {
		//std::*_heap works with max-heaps, grumble
		return left.first->state_size() > right.first->state_size();
	}

	std::uint32_t size_;
	std::vector<Provenance> provenance_;
	//non-owning pointer to the automata held by the closed set's unique_ptr
	std::vector<std::pair<const PackedAutomaton*, Provenance>> queue_;
	//If sparse_hash_set supported non-PODs, we'd use unique_ptr<const PackedAutomaton>
	//here.  But as we never erase anyway, we might as well let things leak.
	google::sparse_hash_set<const PackedAutomaton*, indirect_hash, indirect_equal> closed_;
};

using Result = std::vector<std::tuple<std::unique_ptr<const PackedAutomaton>, Provenance, std::size_t>>;
automaton_type known_gadget(const std::string& name);

#endif /* TOGGLES_REGISTRY_HPP */

