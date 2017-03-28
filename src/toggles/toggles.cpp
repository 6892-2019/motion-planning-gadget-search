#include "precompiled.hpp"
#include "../automaton.hpp"

using location_type = std::uint8_t;
using automaton_type = automaton::Automaton<8>;
using automaton_ptr = typename automaton_type::ptr;

class Gadget {
public:

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
private:
	//TODO: const_ptr?  we shouldn't need to ever modify it
	automaton_ptr a_;
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

int main(int argc, char* argv[]) {
	return 0;
}