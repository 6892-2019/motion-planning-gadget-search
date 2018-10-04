#include "precompiled.hpp"
#include "automaton.hpp"
#include "provenance.hpp"
#include "ops.hpp"
#include "canonicalize.hpp"
#include "gadgetdefs.hpp"
#include "pack.hpp"
#include "hopscotch/hopscotch_set.h"
#include "stringutils.hpp"
#include "maybe_owning_ptr.hpp"
#include "stringutils.hpp"
#include "automaton-io.hpp"
#include <thread>
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#include <jemalloc/jemalloc.h>
#include <boost/process.hpp>
#include <unistd.h>
#include <netinet/sctp.h>

using namespace automaton;
using std::vector;
using std::pair;
using std::string;
using std::unique_ptr;
using std::chrono::duration_cast;

class Stopwatch {
private:
	//https://stackoverflow.com/a/37440647/3614835
	using best_clock = std::conditional_t<std::chrono::high_resolution_clock::is_steady,
			std::chrono::high_resolution_clock,
			std::chrono::steady_clock>;
	struct StopwatchData {
		StopwatchData() : time(best_clock::now()) {
			usage = {};
			getrusage(RUSAGE_SELF, &usage);
		}
		best_clock::time_point time;
		rusage usage;
	};
public:
	class Result {
	public:
		Result(StopwatchData start, StopwatchData end) : start_(start), end_(end) {}
		template<class Duration>
		Duration elapsed() {
			return duration_cast<Duration>(end_.time - start_.time);
		}
		unsigned long seconds() {
			return elapsed<std::chrono::seconds>().count();
		}
		unsigned long millis() {
			return elapsed<std::chrono::milliseconds>().count();
		}
		unsigned long micros() {
			return elapsed<std::chrono::microseconds>().count();
		}
		unsigned long nanos() {
			return elapsed<std::chrono::nanoseconds>().count();
		}
		std::string hms() {
			auto diff = end_.time - start_.time;
			auto hours = duration_cast<std::chrono::hours>(diff);
			auto minutes = duration_cast<std::chrono::minutes>(diff) - hours;
			auto seconds = duration_cast<std::chrono::seconds>(diff) - hours - minutes;
			return std::to_string(hours.count()) + "h" + std::to_string(minutes.count()) + "m" + std::to_string(seconds.count()) + "s";
		}

		template<class Duration>
		Duration userTime() {
			return duration_cast<Duration>(from_timeval(end_.usage.ru_utime) - from_timeval(start_.usage.ru_utime));
		}
		unsigned long userSeconds() {
			return userTime<std::chrono::seconds>().count();
		}
		unsigned long userMillis() {
			return userTime<std::chrono::milliseconds>().count();
		}
		unsigned long userMicros() {
			return userTime<std::chrono::microseconds>().count();
		}
		unsigned long userNanos() {
			return userTime<std::chrono::nanoseconds>().count();
		}
		template<class Duration>
		Duration systemTime() {
			return duration_cast<Duration>(from_timeval(end_.usage.ru_stime) - from_timeval(start_.usage.ru_stime));
		}
		unsigned long systemSeconds() {
			return systemTime<std::chrono::seconds>().count();
		}
		unsigned long systemMillis() {
			return systemTime<std::chrono::milliseconds>().count();
		}
		unsigned long systemMicros() {
			return systemTime<std::chrono::microseconds>().count();
		}
		unsigned long systemNanos() {
			return systemTime<std::chrono::nanoseconds>().count();
		}
		template<class Duration>
		Duration cpuTime() {
			return userTime<Duration>() + systemTime<Duration>();
		}
		unsigned long cpuSeconds() {
			return cpuTime<std::chrono::seconds>().count();
		}
		unsigned long cpuMillis() {
			return cpuTime<std::chrono::milliseconds>().count();
		}
		unsigned long cpuMicros() {
			return cpuTime<std::chrono::microseconds>().count();
		}
		unsigned long cpuNanos() {
			return cpuTime<std::chrono::nanoseconds>().count();
		}

		double utilization() {
			//We can tolerate the potential loss of precision here.
			return static_cast<double>(cpuNanos()) / static_cast<double>(nanos());
		}
	private:
		StopwatchData start_, end_;
	};

	Stopwatch() : data_() {}
	/**
	 * Resets the start point.
	 */
	void reset() {
		data_ = StopwatchData();
	}
	/**
	 * Returns a Result describing the elapsed time and other metrics.  Doesn't
	 * modify this Stopwatch, so can be called repeatedly to measure from the
	 * same start point.
	 */
	Result elapsed() const {
		//Imply to the compiler that it should make the system calls ASAP.
		auto end = StopwatchData();
		return {data_, end};
	}
private:
	StopwatchData data_;

	static std::chrono::microseconds from_timeval(timeval& tv) {
		return std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec);
	}
};

typedef Automaton<8u> automaton_type;
typedef pair<const Pack*, Provenance> PackProv;
using ClosedSet = tsl::hopscotch_set<const Pack*,
		PackHasher, PackEqualer, std::allocator<const Pack*>,
		30, true /* store the hash */>;
typedef unsigned int index_type;

using RotationVec = boost::container::small_vector<unsigned int, automaton_type::alphabet_size_v>;
struct Input {
	index_type index;
	AutomatonBase::symbol_type active_alphabet_size;
	automaton_type normal;
	RotationVec useful_rotations;
};

struct Target {
	std::size_t packed_hash, mirror_packed_hash;
	const Pack* normal, *mirror;
};

class PageHolder {
public:
	PageHolder(std::size_t desiredPageSize) : pageSize_(nallocx(desiredPageSize, 0)) {}
	std::byte* current_begin() const {
		return pages_.back().get();
	}
	std::byte* current_end() const {
		return current_begin() + page_size();
	}
	std::byte* allocate() {
		pages_.emplace_back(static_cast<std::byte*>(std::malloc(pageSize_)));
#ifndef NDEBUG
		std::fill(current_begin(), current_end(), std::byte{0xFF});
#endif //NDEBUG
		return pages_.back().get();
	}
	std::size_t page_size() const {
		return pageSize_;
	}
	std::size_t page_count() const {
		return pages_.size();
	}
	std::size_t total_page_memory() const {
		//doesn't include the PageHolder itself
		return page_size() * page_count();
	}

	std::size_t size() const {
		return pages_.size();
	}
	std::pair<std::byte*, std::byte*> page_bounds(std::size_t p) const {
		if (pages_[p]) return {pages_[p].get(), pages_[p].get() + page_size()};
		return {nullptr, nullptr};
	}
	void release(std::size_t p) {
		pages_[p].reset();
	}
private:
	//should maybe be a small_vector?
	std::vector<std::unique_ptr<std::byte, free_deleter>> pages_;
	std::size_t pageSize_;
};

struct Finisher {
	Finisher(const ClosedSet* closed) : pages(1*1024*1024), cur(pages.allocate()), globalClosed(closed) {}
	Finisher(const Finisher& f, tbb::split) : pages(1*1024*1024), cur(pages.allocate()), globalClosed(f.globalClosed) {}
	vector<PackProv> nextgen;
	PageHolder pages;
	Pack* cur;
	ClosedSet localClosed;
	const ClosedSet* globalClosed;
	unsigned int globalClosedPruned = 0, localClosedPruned = 0;
	std::size_t bytesAdopted = 0;
	void operator()(automaton_type&& a, Provenance p) {
		canonicalize(a, a.active_alphabet_size());
		Pack* new_cur = pack(a, cur, pages.current_end());
		if (!new_cur) {
			//This might waste some space on the old page if we prune this
			//automaton, but it ensures we don't have more than one empty page.
			cur = pages.allocate();
			new_cur = pack(a, cur, pages.current_end());
			if (!new_cur) {
				std::cout << "pack too big?!\n";
				serialize(a, defaultFilename(a));
				return; //This being pathological, don't stop the search for this.
			}
		}

		auto hash = packed_hash(cur);
		//Check the closed set to deduplicate early.
		if (globalClosed && globalClosed->find(cur, hash) != globalClosed->end()) {
			++globalClosedPruned;
			return;
		}
		if (localClosed.insert(cur).second) { //TODO: insert overload taking the hash
			//We could check targets here, but we can't easily report a finding
			//and, once we go parallel, we want to ensure we get a deterministic
			//finding, so we'd have to check that an earlier thread hadn't yet.
			nextgen.emplace_back(cur, p);
			bytesAdopted += numeric_cast<std::size_t>(new_cur - cur);
			cur = new_cur;
		} else
			++localClosedPruned;
	}
	void join(Finisher& rhs) {
		if (nextgen.empty()) {
			//You'd think this shouldn't happen, but it does, both due to global
			//pruning and TBB's overzealous splitting.
			assert(localClosed.empty());
			assert(cur == pages.current_begin());
			assert(localClosedPruned == 0);
			assert(bytesAdopted == 0);
			nextgen = std::move(rhs.nextgen);
			pages = std::move(rhs.pages);
			cur = rhs.cur;
			localClosed = std::move(rhs.localClosed);
			assert(globalClosed == rhs.globalClosed);
			globalClosedPruned += rhs.globalClosedPruned;
			localClosedPruned += rhs.localClosedPruned;
			bytesAdopted += rhs.bytesAdopted;
			return;
		}

//		Stopwatch stopwatch;
//		auto oldbytes = bytesAdopted;
//		auto oldcount = nextgen.size();

		//If we're globally pruning, we did it already.
		assert(((bool)globalClosed) == ((bool)rhs.globalClosed));
		rhs.destructive_for_each_pack([&](PackProv& p) {
			//We can either copy survivors to our PageHolder, or linear-search
			//for and adopt their pages.  Assuming we committed packs in order,
			//we'd only need to keep track of which is the current page and
			//whether we've adopted it.  At the cost of increased memory
			//retention, we could just adopt all the pages, only compacting when
			//committing to the new generation and closed set.
			if (!localClosed.count(p.first)) { //TODO: use saved hash (if we do)
				auto size = packed_size(p.first);
				if (pages.current_end() - cur < size)
					//Above we rejected any packs larger than a page, so we know
					//we won't have any here.
					cur = pages.allocate();
				Pack* pack_starts = cur;
				cur = std::copy(p.first, p.first + size, cur);
				localClosed.insert(pack_starts); //TODO: use hash
				nextgen.emplace_back(pack_starts, p.second);
				bytesAdopted += size;
			} else
				++localClosedPruned;
		});
		globalClosedPruned += rhs.globalClosedPruned;
		localClosedPruned += rhs.localClosedPruned;
		//deliberately don't merge bytesAdopted

//		Stopwatch::Result timing = stopwatch.elapsed();
//		fmt::print("Finisher::join took {}ms; left {} ({}), right {} ({}), now {} ({}).\n",
//				timing.millis(), oldcount, oldbytes, rhs.nextgen.size(), rhs.bytesAdopted,
//				nextgen.size(), bytesAdopted);
	}

	/**
	 * Call the given callable for every PackProv in this->nextgen, freeing
	 * pages when possible.
	 */
	template<class Callable>
	void destructive_for_each_pack(Callable&& callable) {
		//These pointers will become dangling, so may as well clear this now.
		//(I guess we could erase each element after visiting it...)
		localClosed = {};
		cur = nullptr;

		//We're assuming the packs are in the same order in the pages.
		std::size_t pagenumber = 0;
		auto pagebounds = pages.page_bounds(pagenumber);

		for (auto i = nextgen.begin(), end = nextgen.end(); i != end;) {
			callable(*i);
			++i;
			if (i == end || !(pagebounds.first <= i->first && i->first < pagebounds.second)) {
				pages.release(pagenumber);
				++pagenumber;
				if (pagenumber < pages.size())
					pagebounds = pages.page_bounds(pagenumber);
			}
		}
		//dangling, so clear it now
		nextgen.clear();
		nextgen.shrink_to_fit();
	}
};

maybe_owning_ptr<Finisher> indirect_split(const maybe_owning_ptr<Finisher>& f){
	return maybe_owning_ptr<Finisher>(new Finisher(*f, tbb::split{}), true);
}
void indirect_join(const maybe_owning_ptr<Finisher>& lhs, const maybe_owning_ptr<Finisher>& rhs) {
	lhs->join(*rhs);
}

template<class Iter>
void setInitialStatesToAcceptingStatesInRange(automaton_type& a, Iter first, Iter last) {
	using state_type = automaton_type::state_type;
	state_type s = a.addState();
	for (state_type t : make_range_for_pair(first, last))
		if (a.accept(t))
			a.addEpsilon(s, t);
	a.swapStateNumbers(0, s);
}

bool enjoin(automaton_type& a, automaton_type::symbol_type l, automaton_type::symbol_type m) {
	using state_type = automaton_type::state_type;
	bool progress, changed = false;
	//TODO: instead of fixpoint iteration, we should put the changed state s
	//on a worklist and iterate until it's empty
	do {
		progress = false;
		for (state_type s = 0; s < a.state_size(); ++s) {
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
}

template<class State, typename SplitFunc, typename EvalFunc, typename JoinFunc>
struct MutableReduce {
	const SplitFunc* split_;
	const EvalFunc* eval_;
	const JoinFunc* join_;
	State state_;
	template<class S>
	MutableReduce(S&& initialState, SplitFunc& split, EvalFunc& eval, JoinFunc& join) :
			split_(&split), eval_(&eval), join_(&join), state_(std::forward<S>(initialState)) {}
	MutableReduce(MutableReduce& lhs, tbb::split) : split_(lhs.split_), eval_(lhs.eval_),
			join_(lhs.join_), state_((*split_)(lhs.state_)) {}
	template<class Range>
	void operator()(const Range& r) {
		(*eval_)(r, state_);
	}
	void join(MutableReduce& rhs) {
		(*join_)(state_, rhs.state_);
	}
};

template<class Range, class State, typename SplitFunc, typename EvalFunc, typename JoinFunc>
auto parallel_reduce(Range range, State&& initialState, SplitFunc splitter, EvalFunc eval, JoinFunc joiner) {
	MutableReduce<State, SplitFunc, EvalFunc, JoinFunc> body(std::forward<State>(initialState), splitter, eval, joiner);
	tbb::parallel_reduce(range, body);
	return std::move(body.state_);
}

auto connect_alphamap(unsigned int locations, unsigned int connectPoint) {
	//TODO: these alphamap manipulations could all be precomputed, though it's
	//not clear that would be any faster than using a stack variable
	std::array<unsigned int, automaton_type::alphabet_size_v> alphamap;
	if (connectPoint+1 == locations) {
		auto end = alphamap.begin()+locations-2;
		//other connect point is zero, so start from 1
		std::iota(alphamap.begin(), end, 1);
		std::fill(end, alphamap.end(), std::numeric_limits<automaton_type::symbol_type>::max());
	} else {
		auto middle = alphamap.begin()+connectPoint, end = alphamap.begin()+locations-2;
		std::iota(alphamap.begin(), middle, 0);
		std::iota(middle, end, connectPoint+2);
		std::fill(end, alphamap.end(), std::numeric_limits<automaton_type::symbol_type>::max());
	}
	return alphamap;
}

template<class ForEachDestination, class IsAccept>
auto predecessorless_accept_things(const unsigned int size,
		ForEachDestination&& for_each_destination, IsAccept&& accept) {
	dynarray<unsigned int> predcount(size);
	std::fill(predcount.begin(), predcount.end(), 0u);
	for (auto s : xrange(size))
		for_each_destination(s, [&](unsigned int t){++predcount[t];});

	//We don't want predecessorless nonaccept states to count as predecessors.
	bool progress = true;
	while (progress) {
		progress = false;
		for (unsigned int i = 0; i < size; ++i)
			if (predcount[i] == 0 && !accept(i)) {
				for_each_destination(i, [&](unsigned int t){--predcount[t];});
				//Mark this state as previously considered, not to be repeated.
				predcount[i] = std::numeric_limits<unsigned int>::max();
				progress = true;
			}
	}

	std::vector<unsigned int> retval;
	for (unsigned int i = 0; i < size; ++i)
		if (predcount[i] == 0 && accept(i))
			retval.push_back(i);
	return retval;
}
auto predecessorless_accept_states(const automaton_type& a) {
	return predecessorless_accept_things(a.state_size(),
			[&](unsigned int s, auto&& action){a.for_each_destination(s, action);},
			[&](unsigned int s){return a.accept(s);});
}
auto predecessorless_accept_components(const automaton_type& a, const SCCs& sccs) {
	dynarray<unsigned int> state_to_comp(a.state_size());
	for (auto c : xrange(sccs.size()))
		for (auto s : make_range_for_pair(sccs.begin(c), sccs.end(c)))
			state_to_comp[s] = c;
	return predecessorless_accept_things(sccs.size(),
			[&](unsigned int c, auto&& action){
				//We might visit a destination many times if it's targeted by
				//multiple states in the source component, but that's fine as
				//long as we're consistent between increments and decrements.
				for (auto s : make_range_for_pair(sccs.begin(c), sccs.end(c)))
					a.for_each_destination(s, [&](automaton_type::state_type t) {
						if (state_to_comp[t] != c) //only count edges to other components
							action(state_to_comp[t]);
					});
			},
			[&](unsigned int c) {
				return std::any_of(sccs.begin(c), sccs.end(c), [&](unsigned int s){
					return a.accept(s);
				});
			});
}

void connect_at(const automaton_type& a, std::uint32_t gadgetIndex, const Provenance* combineData, //could also be optional<Provenance>
		unsigned int locations, unsigned int connectPoint, Finisher& finisher) {
	automaton_type connected = a;
	enjoin(connected, connectPoint, (connectPoint+1) % locations); //TODO: maybe branch instead of modulo
	acceptingClosure(connected, locations);
	auto alphamap = connect_alphamap(locations, connectPoint);
	connected.renumberAlphabet(alphamap.begin());

	//We may have disconnected the automaton (disconnecting the
	//configuration graph of the gadget it represents).
	automaton::SCCs sccs = automaton::find_components(connected);
	auto roots = predecessorless_accept_components(connected, sccs);
	//TODO: don't reduce if just one; don't reduce over singleton components (?)

	parallel_reduce(tbb::blocked_range<unsigned int>(0, static_cast<unsigned int>(roots.size())),
			maybe_owning_ptr<Finisher>(&finisher, false),
			indirect_split,
			[&](const tbb::blocked_range<unsigned int>& r, maybe_owning_ptr<Finisher>& finish) {
				std::array<automaton_type::symbol_type, automaton_type::alphabet_size_v> compression;
				for (unsigned int c = r.begin(); c != r.end(); ++c) {
					//If there are no accept states in the component, it
					//represents the empty language, and we can skip it.  (There
					//don't seem to be any non-singleton components having no
					//accept states, so we only check singletons.)
//					if (sccs.end(c) - sccs.begin(c) == 1 && !connected.accept(*sccs.begin(c))) continue;

					automaton_type op = connected;
//					setInitialStatesToAcceptingStatesInRange(op, sccs.begin(c), sccs.end(c));
					setInitialStatesToAcceptingStatesInRange(op, sccs.begin(roots[c]), sccs.end(roots[c]));
					op.minimize();
					automaton::AutomatonBase::SymbolSet active = op.activeAlphabet();
					if (active.size() <= 1) continue; //there are no interesting 1-symbol automata
					if (active.size() != (locations - 2)) {
						//compress the alphabet
						active.sort();
						std::copy(active.begin(), active.end(), compression.begin());
						std::fill(compression.begin()+active.size(), compression.end(), std::numeric_limits<automaton_type::symbol_type>::max());
						op.renumberAlphabet(compression.begin());
						//Because we're deleting unused symbols, we don't need to
						//minimize again; any two equivalent states would differ only in
						//the symbols we deleted, but those symbols were inactive.
					}
					if (combineData)
						(*finish)(std::move(op), Provenance::connect(*combineData, connectPoint, c));
					else
						(*finish)(std::move(op), Provenance::connect(gadgetIndex, connectPoint, c));
				}
			},
			indirect_join);
}

void connect(const automaton_type& a, std::uint32_t gadgetIndex, unsigned int locations, Finisher& finisher) {
	parallel_reduce(tbb::blocked_range<unsigned int>(0, locations),
			maybe_owning_ptr<Finisher>(&finisher, false),
			indirect_split,
			[&](const tbb::blocked_range<unsigned int>& r, maybe_owning_ptr<Finisher>& finish) {
				for (unsigned int l = r.begin(); l < r.end(); ++l)
					connect_at(a, gadgetIndex, nullptr, locations, l, *finish);
			},
			indirect_join);
}

void combine(const automaton_type& la, uint32_t l, automaton_type::state_type leftLocations,
		const automaton_type& ra, uint32_t r, automaton_type::state_type rightLocations,
		const RotationVec& rightRotations, Finisher& finish) {
	using symbol_type = automaton_type::symbol_type;
	std::array<symbol_type, automaton_type::alphabet_size_v> slide;
	automaton_type shiftedRight = ra;
	std::iota(slide.begin(), slide.end(), 0);
	std::rotate(slide.rbegin(), slide.rbegin()+leftLocations, slide.rend());
	shiftedRight.renumberAlphabet(slide.data());
	automaton_type shuffled = automaton::shuffleAccept(la, shiftedRight);
	shuffled.minimize();
	//Now [0,leftLocations) are from the left and [leftLocations,leftLocations+rightLocations)
	//are from the right.  We want to start inserting at left location 0, so we
	//write all the right locations, then all the left locations.  We'll rotate
	//the right locations as appropriate.  Then we'll move a left location to
	//the other end of the array.  Locations beyond leftLocations+rightLocations
	//are left alone, as they are always inactive.
	std::iota(slide.begin(), slide.begin()+rightLocations, leftLocations);
	std::iota(slide.begin()+rightLocations, slide.begin()+rightLocations+leftLocations, 0);
	std::iota(slide.begin()+rightLocations+leftLocations, slide.end(), rightLocations+leftLocations);
	for (decltype(leftLocations) ll = 0; ll < leftLocations; ++ll) {
		for (auto rotation : rightRotations) {
			//Because we're reading a list of rotations (not rotation deltas),
			//we have to re-initialize the right locations each time.
			std::iota(slide.begin()+ll, slide.begin()+ll+rightLocations, leftLocations);
			std::rotate(slide.begin()+ll, slide.begin()+ll+rotation, slide.begin()+ll+rightLocations);
			automaton_type permuted = shuffled;
			permuted.permuteAlphabet(slide.data());
			Provenance prov = Provenance::combine(l, r, ll, rotation);
			connect_at(permuted, std::numeric_limits<std::uint32_t>::max(), &prov, leftLocations+rightLocations,
					(leftLocations+rightLocations+ll-1) % (leftLocations + rightLocations), finish);
			connect_at(permuted, std::numeric_limits<std::uint32_t>::max(), &prov, leftLocations+rightLocations,
					(leftLocations+rightLocations+ll+rightLocations-1) % (leftLocations + rightLocations), finish);
		}
		std::swap(slide[ll], slide[ll+rightLocations]);
	}
}

auto find_useful_rotations(const automaton_type& a) {
	using symbol_type = automaton_type::symbol_type;
	RotationVec useful;
	auto locations = a.active_alphabet_size();
	if (!locations) return useful;

	PageHolder pages(16*1024*1024);
	std::byte* cur = pages.allocate();
	ClosedSet closed;
	std::array<symbol_type, automaton_type::alphabet_size_v> rotation;
	std::iota(rotation.begin(), rotation.end(), 0);
	for (unsigned int rl = 0; rl < locations; ++rl) {
		//It's arbitrary which way we rotate so long as we match what combine does.
		std::iota(rotation.begin(), rotation.begin()+locations, 0);
		std::rotate(rotation.begin(), rotation.begin()+rl, rotation.begin()+locations);
		automaton_type rm = a;
		rm.permuteAlphabet(rotation);
		rm.canonicalize(); //The normal, non-alphabet-adjusting canonicalize.
		auto p = pack(rm, cur, pages.current_end());
		if (!p) {
			std::cout << "I guess 16MB wasn't enough for everybody.\n";
			std::terminate();
		}
		//if we haven't seen it before
		if (closed.insert(cur).second)
			useful.push_back(rl);
		cur = p;
	}
	return useful;
}

class GenerationalSearch {
public:
	GenerationalSearch(vector<pair<std::string_view, automaton_type>>& inputs,
			vector<pair<std::string_view, automaton_type>>& targets)
			: pages_(256*1024*1024), cur_(pages_.allocate()) {
		for (auto& [name, a] : inputs) {
			auto asz = a.active_alphabet_size();
			auto normal_rotations = find_useful_rotations(a);
			inputs_.push_back({numeric_cast<index_type>(inputs_.size()), asz, a, normal_rotations});
			fmt::print("input {}: {}, size {}, rotations {}\n",
					inputs_.back().index, name, inputs_.back().active_alphabet_size, inputs_.back().useful_rotations);

			automaton_type m = mirror(a);
			if (m != a) {
				auto mirror_rotations = find_useful_rotations(m);
				inputs_.push_back({numeric_cast<index_type>(inputs_.size()), asz, m, mirror_rotations});
				fmt::print("input {}: {} (mirrored), size {}, rotations {}\n",
						inputs_.back().index, name, inputs_.back().active_alphabet_size, inputs_.back().useful_rotations);
			}
		}
		targets_.reserve(targets.size());
		for (auto& [name, t] : targets) {
			auto normalPack = cur_;
			auto mirrorPack = pack(t, normalPack, pages_.current_end());
			cur_ = pack(mirror(t), mirrorPack, pages_.current_end());
			targets_.push_back({packed_hash(normalPack), packed_hash(mirrorPack), normalPack, mirrorPack});
			fmt::print("target {}: {}\n", targets_.size()-1, name);
		}
	}
	virtual ~GenerationalSearch() = default;
	void advance() {
		Stopwatch stopwatch;
		subgeneration_requested_ = generation_requested_ = false;
		do_combine();
		do_connect();
		Stopwatch::Result timing = stopwatch.elapsed();
		fmt::print("Finished generation {} in {} ({}); produced {}, closed size {}.\n",
				generation_, timing.hms(), timing.utilization(), curgen_.size(), closed_.size());
		++generation_;
		if (!generation_requested_) std::exit(0);
	}
protected:
	PageHolder pages_;
	std::byte* cur_;
	vector<const Pack*> curgen_; //non-owning, stored in pages_ and already in closed_
	vector<Provenance> provenance_;
	ClosedSet closed_;
	vector<Input> inputs_;
	vector<Target> targets_;
	unsigned int generation_ = 0;
	bool subgeneration_requested_ = false, generation_requested_ = false;

	void do_combine() {
		Stopwatch stopwatch;
		Finisher finisher(&closed_);
		if (generation_ == 0) {
			assert(closed_.empty());
			//"combine against nothing" to get started.  This includes mirrored
			//inputs, but that's safe and not too wasteful.
			for (auto& i : inputs_)
				finisher(automaton_type{i.normal}, Provenance::input(i.index));
		} else {
			index_type sourceIndexBase = numeric_cast<index_type>(provenance_.size()-curgen_.size());
			parallel_reduce(tbb::blocked_range<std::size_t>(0, curgen_.size()),
					maybe_owning_ptr<Finisher>(&finisher, false),
					indirect_split,
					[&](const tbb::blocked_range<std::size_t>& r, maybe_owning_ptr<Finisher>& finish) {
						for (std::size_t i = r.begin(); i < r.end(); ++i)
							combine_once(curgen_[i], static_cast<unsigned int>(sourceIndexBase + i), *finish);
					},
					indirect_join);
		}
		curgen_.clear();
		append(finisher);

		Stopwatch::Result timing = stopwatch.elapsed();
		//TODO: total size, summary stats of produced or the entire closed set?
		fmt::print("Finished combine {} in {} ({}); "
				"produced {}, globally pruned {}, locally pruned {}, closed size {}.\n",
				generation_, timing.hms(), timing.utilization(), curgen_.size(),
				finisher.globalClosedPruned, finisher.localClosedPruned, closed_.size());
	}

	void combine_once(const Pack* source, index_type sourceIndex, Finisher& finishAction) {
		//from toggles.cpp's Combine::operator(); TODO: may want to reunify
		automaton_type unpacked;
		unpack(unpacked, source);
		automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
		for (const Input& i : inputs_) {
			if (leftLocations + i.active_alphabet_size > automaton_type::alphabet_size_v) continue;
			combine(unpacked, sourceIndex, leftLocations, i.normal, i.index, i.active_alphabet_size, i.useful_rotations, finishAction);
		}
	}

	void do_connect() {
		Stopwatch connectwatch;
		std::size_t newStart = 0;
		unsigned int subgeneration = 0;
		std::size_t totalProduced = 0, totalGlobalPruned = 0, totalLocalPruned = 0;
		while (subgeneration_requested_) {
			Stopwatch subgenwatch;
			Finisher finisher(&closed_);
			index_type sourceIndexBase = numeric_cast<index_type>(provenance_.size()-(curgen_.size()-newStart));
			parallel_reduce(tbb::blocked_range<std::size_t>(newStart, curgen_.size()),
					maybe_owning_ptr<Finisher>(&finisher, false),
					[](const maybe_owning_ptr<Finisher>& f){return maybe_owning_ptr<Finisher>(new Finisher(*f, tbb::split{}), true);},
					[&](const tbb::blocked_range<std::size_t>& r, maybe_owning_ptr<Finisher>& finish) {
						for (std::size_t i = r.begin(); i < r.end(); ++i) {
							index_type sourceIndex = numeric_cast<index_type>(sourceIndexBase + (i-newStart));
							automaton_type inflated;
							unpack(inflated, curgen_[i]);
							automaton_type::symbol_type locations = inflated.active_alphabet_size();
							connect(inflated, sourceIndex, locations, *finish);
						}
					},
					[](const maybe_owning_ptr<Finisher>& lhs, const maybe_owning_ptr<Finisher>& rhs) {
						lhs->join(*rhs);
					});
			std::size_t produced = finisher.nextgen.size();
			newStart = append(finisher);
			Stopwatch::Result timing = subgenwatch.elapsed();
			fmt::print("Finished connect {}.{} in {} ({}); produced {}, globally pruned {}, locally pruned {}, closed size {}.\n",
				generation_, subgeneration, timing.hms(), timing.utilization(),
				produced, finisher.globalClosedPruned, finisher.localClosedPruned, closed_.size());
			++subgeneration;
			totalProduced += produced;
			totalGlobalPruned += finisher.globalClosedPruned;
			totalLocalPruned += finisher.localClosedPruned;
		}
		Stopwatch::Result timing = connectwatch.elapsed();
		fmt::print("Finished connect {} in {} ({}); total: produced {}, globally pruned {}, locally pruned {}.\n",
				generation_, timing.hms(), timing.utilization(),
				totalProduced, totalGlobalPruned, totalLocalPruned);
	}

	virtual std::size_t append(Finisher& finisher) {
//		Stopwatch stopwatch;
//		std::size_t sizeConsidered = 0, sizeCommitted = 0;
//		auto oldClosedSize = closed_.size();

		auto newStart = curgen_.size();
		finisher.destructive_for_each_pack([&](PackProv& p) {
			auto size = packed_size(p.first);
//			sizeConsidered += size;
			auto hash = packed_hash(p.first);
			//If we're globally pruning in Finisher, this should always succeed.
			if (!closed_.count(p.first, hash)) {
				assert(size < pages_.page_size());
				for (auto i : xrange(targets_.size())) {
					const Target& t = targets_[i];
					if (t.packed_hash == hash || t.mirror_packed_hash == hash)
						fmt::print("found target {}: {}\n", i, p.second);
				}
				if (pages_.current_end() - cur_ < size)
					cur_ = pages_.allocate();
				Pack* pack_starts = cur_;
				cur_ = std::copy(p.first, p.first+size, cur_);
				closed_.insert(pack_starts); //TODO: use hash
				curgen_.push_back(pack_starts);
				provenance_.push_back(p.second);
//				sizeCommitted += size;
			}
		});

//		Stopwatch::Result timing = stopwatch.elapsed();
//		fmt::print("append took {}ms; curgen {} -> {}, closed {} -> {}; considered {} ({}), committed {} ({}), ratio {} ({}).\n",
//				timing.millis(), newStart, curgen_.size(), oldClosedSize, closed_.size(),
//				next.size(), sizeConsidered, closed_.size() - oldClosedSize, sizeCommitted,
//				((double)(closed_.size() - oldClosedSize))/next.size(), ((double)sizeCommitted)/sizeConsidered);
		subgeneration_requested_ = curgen_.size() != newStart;
		generation_requested_ |= subgeneration_requested_;
		return newStart;
	}
};

class DistributedGenerationalSearch : public GenerationalSearch {
public:
	DistributedGenerationalSearch(vector<pair<std::string_view, automaton_type>>& inputs,
			vector<pair<std::string_view, automaton_type>>& targets,
			std::vector<std::string>& hosts)
			: GenerationalSearch(inputs, targets), hostnames_(hosts) {
		std::sort(hostnames_.begin(), hostnames_.end());
		std::string localhost = localhostname();
		machine_id_ = numeric_cast<decltype(machine_id_)>(
				std::find(hostnames_.begin(), hostnames_.end(), localhost) - hostnames_.begin());
		assoc_.assign(hostnames_.size(), std::numeric_limits<sctp_assoc_t>::max());

		addresses_.reserve(hostnames_.size());
		for (const auto& name : hostnames_)
			addresses_.push_back(get_address(name));

		socket_ = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
		if (socket_ < 0) {
			perror("socket");
			std::exit(1);
		}

		//We need this to get sndrcvinfo filled in later.
		sctp_event_subscribe events = {};
		events.sctp_data_io_event = 1;
		if (setsockopt(socket_, SOL_SCTP, SCTP_EVENTS, &events, sizeof(events)) < 0) {
			perror("setsockopt SCTP_EVENTS");
			std::exit(1);
		}
		int fragment_interleave = 0;
		if (setsockopt(socket_, SOL_SCTP, SCTP_FRAGMENT_INTERLEAVE, &fragment_interleave, sizeof(fragment_interleave)) < 0) {
			perror("setsockopt SCTP_FRAGMENT_INTERLEAVE");
			std::exit(1);
		}

		sockaddr_in bind_addr;
		memset(&bind_addr, 0, sizeof(bind_addr));
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_port = htons(12000);
		bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(socket_, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
			perror("bind");
			std::exit(1);
		}

		if (listen(socket_, 1) < 0) {
			perror("listen");
			std::exit(1);
		}

		//Form an association to ourselves and all machines preceding us.
		//(Connecting to a node that we've already associated with is an error
		//that doesn't fill in the association id, so we can't just do the
		//obvious all-to-all here.)
		for (unsigned int i = 0; i <= machine_id_; ++i) {
			sctp_assoc_t assoc;
			Stopwatch timer;
			int rc = 0, savederrno = 0;
			//Wait a bit before giving up if the peer isn't listening yet.
			do {
				rc = sctp_connectx(socket_, (sockaddr*)&addresses_[i], 1, &assoc);
				savederrno = errno;
			} while (rc && savederrno == ECONNREFUSED && timer.elapsed().seconds() < 3);
			if (rc) {
				fmt::print("sctp_connectx: {} ({})\n", strerror(savederrno), savederrno);
				std::exit(1);
			}
			assoc_[i] = assoc;
		}

		//Get the association id for successors (that connected to us).  We'll
		//retry this up to a timeout because we're racing the inbound connects.
		for (unsigned int i = machine_id_+1; i < assoc_.size(); ++i) {
			Stopwatch timer;
			do {
				sctp_paddrinfo info = {};
				std::memcpy(&info.spinfo_address, &addresses_[i], sizeof(sockaddr_in));
				socklen_t length = sizeof(info);
				if (getsockopt(socket_, SOL_SCTP, SCTP_GET_PEER_ADDR_INFO, &info, &length) >= 0)
					assoc_[i] = info.spinfo_assoc_id;
			} while (assoc_[i] == std::numeric_limits<sctp_assoc_t>::max() && timer.elapsed().seconds() < 3);
			if (assoc_[i] == std::numeric_limits<sctp_assoc_t>::max()) {
				fmt::print("{}: unable to get assoc id for {}\n", localhost, hostnames_[i]);
				std::exit(1);
			}
		}

		//Set the default context for received messages to the sender's machine id.
		for (unsigned int i = 0; i < assoc_.size(); ++i) {
			sctp_assoc_value defctx = {assoc_[i], i};
			if (setsockopt(socket_, SOL_SCTP, SCTP_CONTEXT, &defctx, sizeof(defctx)) < 0) {
				perror("setsockopt SCTP_CONTEXT");
				std::exit(1);
			}
		}

		fmt::print("{} successfully associated with {}\n", localhost, hostnames_);
	}

protected:
	std::size_t append(Finisher& finisher) override {
		auto newStart = curgen_.size();
		broadcast_control(ControlMsg::READY);
		std::thread recv(&DistributedGenerationalSearch::recv_thread, this, std::ref(finisher));
		recv.join();

		if (machine_id_ == 0) {
			broadcast_finished(true); //finish the broadcast
			vote(curgen_.size() != newStart);
			count_votes();
		} else
			vote(curgen_.size() != newStart);
		subgeneration_requested_ = learn_result();

		generation_requested_ |= subgeneration_requested_;
		fmt::print("{} result is {}\n", hostnames_[machine_id_], subgeneration_requested_);
		return newStart;
	}

private:
	int socket_;
	std::vector<sctp_assoc_t> assoc_;
	std::uint8_t machine_id_;
	std::vector<std::string> hostnames_;
	std::vector<sockaddr_in> addresses_;
	//A buffer for ready messages that arrive in learn_result.  The recv thread
	//processes these before anything else.  This is not explicitly synchronized,
	//but learn_result returns before the recv thread starts (in the next
	//subgeneration), and the recv thread is joined before learn_result is called.
	//If not for the ready message, we'd have to buffer (variable-length) packs.
	//(We're storing just the source here, as that's all we care about.)
	std::vector<std::uint8_t> ready_buffer_;

	void send_thread(Finisher& finisher) {
		//The send thread just blasts packs.

		//TODO: ideally we'd have iovec support so we didn't need this.
		dynarray<std::byte> buf(1*1024*1024);
		//Division instructions are faster when the divisor is small, so help
		//the compiler out by truncating it here.  (C++'s promotion rules will
		//still promote it to int, but that should be undoable.)
		std::uint8_t modulus = numeric_cast<std::uint8_t>(assoc_.size());
		finisher.destructive_for_each_pack([&](PackProv& p) {
			p.second.machineId = machine_id_; //produced here
			auto hash = packed_hash(p.first);
			auto shard = hash % modulus;
			send_pack(hash, p.second, p.first, shard, buf);
		});

		broadcast_finished();
	}

	void recv_thread(Finisher& finisher) {
		//The recv thread attempts to insert received packs.  It also does some
		//bookkeeping: it waits for all nodes to be ready before launching the
		//send thread, and terminates when all nodes have finished sending packs.
		//We don't use the Finisher at all except to pass it to the send thread.
		std::thread send; //declared here, but not started until ready.all()
		boost::dynamic_bitset<std::size_t> ready(assoc_.size()), finished(assoc_.size());
		dynarray<std::byte> buf(1*1024*1024);

		auto processReady = [&](std::uint8_t source) {
			bool old = ready.test_set(source);
			if (old) //TODO: unlikely
				fmt::print("{} received duplicate or misaddressed ready from {}\n",
						hostnames_[machine_id_], hostnames_[source]);
			if (ready.all() && !send.joinable())
				send = std::thread(&DistributedGenerationalSearch::send_thread, this, std::ref(finisher));
		};

		//See comment at ready_buffer_ declaration.
		for (std::uint8_t s : ready_buffer_)
			processReady(s);
		ready_buffer_.clear();

		while (true) {
			AnyMsg msg = recv(buf.begin(), buf.size());

			if (ControlMsg* c = std::get_if<ControlMsg>(&msg)) {
				bool old;
				switch (c->msg) {
					case ControlMsg::READY:
						processReady(c->source);
						break;

					case ControlMsg::FINISHED:
						old = finished.test_set(c->source);
						if (old) //TODO: unlikely
							fmt::print("{} received duplicate or misaddressed finished from {}\n",
									hostnames_[machine_id_], hostnames_[c->source]);
						if (finished.all())
							goto break_infinite_loop;
						break;

					default:
						fmt::print("{} ignoring unexpected control message {} from {}\n",
								hostnames_[machine_id_], c->msg, hostnames_[c->source]);
				}
			} else if (PackMsg* p = std::get_if<PackMsg>(&msg)) {
				//the usual append stuff:
				//TODO: maybe we can refactor this along with the nondistributed search's
				if (!closed_.count(p->pack, p->hash)) {
					auto size = packed_size(p->pack);
//					auto resize = p->end - p->pack;
//					auto rehash = packed_hash(p->pack);
//					fmt::print("{} {} {} {} {}\n", size, resize, p->hash, rehash, p->prov);
					assert(size < pages_.page_size());
					for (auto i : xrange(targets_.size())) {
						const Target& t = targets_[i];
						if (t.packed_hash == p->hash || t.mirror_packed_hash == p->hash)
							fmt::print("found target {}: {}\n", i, p->prov);
					}
					if (pages_.current_end() - cur_ < size)
						cur_ = pages_.allocate();
					Pack* pack_starts = cur_;
					cur_ = std::copy(p->pack, p->pack + size, cur_);
					closed_.insert(pack_starts); //TODO: use hash
					curgen_.push_back(pack_starts);
					provenance_.push_back(p->prov);
				}
			} else { //TODO: unlikely
				fmt::print("unhandled message type in recv_thread?!");
				std::exit(1);
			}
		}
		break_infinite_loop:

		send.join();
	}

	void broadcast_finished(bool ready_for_votes = false) {
		fmt::print("{} finishing\n", hostnames_[machine_id_]);
		//Broadcast to all nodes that we're done sending packs.  This is ordered
		//with respect to the packs because it's in the same stream.
		for (auto i : xrange(assoc_.size())) {
			//Node 0 has special handling.  At first we only message ourselves.
			//Then when we're ready to accept votes (after receiving finished
			//messages from all other nodes), we complete the broadcast.
			//If we're finishing an earlier broadcast, skip messaging ourselves
			if (machine_id_ == 0 && i == 0 && ready_for_votes) continue;
			//If this is the first broadcast, stop after messaging ourselves.
			if (machine_id_ == 0 && i > 0 && !ready_for_votes) break;

			send_control(ControlMsg::FINISHED, i);
		}
	}

	void vote(bool want_subgen) {
		send_control(want_subgen ? ControlMsg::VOTE_AYE : ControlMsg::VOTE_NAY, 0);
	}

	void count_votes() {
		boost::dynamic_bitset<std::size_t> voted(assoc_.size()), wants(assoc_.size());
		while (true) {
			ControlMsg m = recv_control();
			unsigned int voter = m.source;
			bool vote = m.msg == ControlMsg::VOTE_AYE;
			fmt::print("vote: {} {} ({})\n", hostnames_[voter], vote, m.msg);
			voted[voter] = true;
			wants[voter] = vote;
			if (voted.all())
				break;
		}

		bool result = wants.any();
		fmt::print("voting result is {}\n", result);
		broadcast_control(result ? ControlMsg::RESULT_AYE : ControlMsg::RESULT_NAY);
	}

	bool learn_result() {
		while (true) {
			ControlMsg m = recv_control();
			//See comment at ready_buffer_ declaration.
			if (m.msg == ControlMsg::READY) {
				ready_buffer_.push_back(m.source);
				continue;
			}
			if (m.source != 0 || (m.msg != ControlMsg::RESULT_AYE && m.msg != ControlMsg::RESULT_NAY)) //TODO: unlikely
				fmt::print("{} got bad result message {} from {}\n", hostnames_[machine_id_], m.msg, hostnames_[m.source]);
			return m.msg == ControlMsg::RESULT_AYE;
		}
	}

	struct Msg {
		sctp_assoc_t assoc;
		std::uint8_t source;
		//Other things we could put here if we start using them:
//		std::uint16_t stream;
//		std::uint32_t ppid;
	};
	struct PackMsg : Msg {
		std::size_t hash;
		Provenance prov;
		std::byte* pack;
		std::byte* end; //for redundant checks against packed_size
	};
	struct ControlMsg : Msg {
		//ready to receive packs
		constexpr static std::byte READY = std::byte{0};
		//finished sending packs
		constexpr static std::byte FINISHED = std::byte{1};
		constexpr static std::byte VOTE_AYE = std::byte{2};
		constexpr static std::byte VOTE_NAY = std::byte{3};
		constexpr static std::byte RESULT_AYE = std::byte{4};
		constexpr static std::byte RESULT_NAY = std::byte{5};
		std::byte msg;
	};
	using AnyMsg = std::variant<PackMsg, ControlMsg>;

	AnyMsg recv(std::byte* buf, std::size_t len) {
		sctp_sndrcvinfo info = {};
		int flags = 0;
		unsigned int total_read = 0;
		//Partial reads are explicitly possible if the stack is "short on buffers".
		//We disabled interleaving on this socket.
		while (!(flags & MSG_EOR)) {
			//TODO: if the buffer's empty, complain and/or discard the partial packet
			int delta_read = sctp_recvmsg(socket_, buf+total_read, len-total_read,
					nullptr, nullptr, //we ignore the socket address in favor of the assoc id
					&info, &flags);
			if (delta_read < 0) //TODO: unlikely
				perror("sctp_recvmsg");
			total_read += delta_read;
		}

		if (total_read < 1) { //unlikely
			fmt::print("{} received message too short {} from {}\n", hostnames_[machine_id_],
					total_read, hostnames_[info.sinfo_context]);
			std::exit(1); //dunno what to do here
		}

		if (total_read == 1) {
			ControlMsg m = {};
			common_msg_init(m, info);
			m.msg = buf[0];
			//TODO: check m.msg is valid message type
			return m;
		}

		if (total_read < sizeof(PackMsg::hash) + sizeof(PackMsg::prov) + 4 /* min pack size */) { //unlikely
			fmt::print("{} received message too short {} from {}\n", hostnames_[machine_id_],
					total_read, hostnames_[info.sinfo_context]);
			std::exit(1); //dunno what to do here
		}

		PackMsg m;
		common_msg_init(m, info);
		m.end = buf + total_read;
		buf = unbuild(buf, &m.hash, sizeof(m.hash));
		m.pack = unbuild(buf, &m.prov, sizeof(m.prov));
		return m;
	}
	ControlMsg recv_control() {
		std::byte buf;
		AnyMsg m = recv(&buf, sizeof(buf));
		if (auto pcm = std::get_if<ControlMsg>(&m))
			return *pcm;
		fmt::print("{} received pack when expecting control message\n", hostnames_[machine_id_]);
		std::exit(1);
	}

	void send_control(std::byte msg, unsigned long dest) {
		sctp_sndrcvinfo info = {};
		info.sinfo_assoc_id = assoc_[dest];
		int sent = sctp_send(socket_, &msg, sizeof(msg), &info, MSG_EOR);
		if (sent < numeric_cast<int>(sizeof(msg))) {//TODO: unlikely
			auto savederrno = errno;
			fmt::print("Problem sending control message {} from {} to {}: {} {}\n",
					msg, hostnames_[machine_id_], hostnames_[dest], msg, strerror(savederrno));
		}
	}
	void broadcast_control(std::byte msg) {
		for (auto i : xrange(assoc_.size()))
			send_control(msg, i);
	}

	static void common_msg_init(Msg& m, sctp_sndrcvinfo& info) {
		m.assoc = info.sinfo_assoc_id;
		m.source = numeric_cast<std::uint8_t>(info.sinfo_context);
	}

	void send_pack(std::size_t hash, Provenance prov, const std::byte* pack,
			unsigned long shard, dynarray<std::byte>& buf) {
		auto size = packed_size(pack);
		if (size + sizeof(hash) + sizeof(prov) > buf.size()) //TODO: unlikely macro
			fmt::print("{} skipping oversized pack {}\n", hostnames_[machine_id_], size);

		auto next = build(buf.begin(), &hash, sizeof(hash));
		next = build(next, &prov, sizeof(prov));
		next = build(next, pack, size);
		std::size_t len = static_cast<std::size_t>(next - buf.begin());

		sctp_sndrcvinfo info = {};
		info.sinfo_assoc_id = assoc_[shard];
		int sent = sctp_send(socket_, buf.begin(), len, &info, MSG_EOR);
		if (sent < 0) //TODO: unlikely
			perror("sctp_send while sending packs");
		if (static_cast<std::size_t>(sent) < len) //TODO: unlikely
			fmt::print("{} short pack write? wrote {} of {}\n", hostnames_[machine_id_], sent, len);
	}

	static std::byte* build(std::byte* dest, const void* src, int count) {
		std::memcpy(dest, src, count);
		return dest + count;
	}
	static std::byte* unbuild(std::byte* src, void* dest, int count) {
		std::memcpy(dest, src, count);
		return src + count;
	}

	static std::string localhostname() {
		char name[HOST_NAME_MAX+1];
		if (gethostname(name, sizeof(name))) {
			perror("gethostname");
			std::exit(1); //environment is not sane
		}
		return name;
	}

	static sockaddr_in get_address(const std::string& hostname) {
		//This assumes there's only going to be one address, or at least that
		//the first one is all we need.
		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_SEQPACKET;
		hints.ai_protocol = IPPROTO_SCTP;
		hints.ai_flags = AI_NUMERICSERV;
		addrinfo* infos;
		int rc = getaddrinfo(hostname.c_str(), "12000", &hints, &infos);
		if (rc) {
			fmt::print("getaddrinfo: {}\n", gai_strerror(rc));
			std::exit(1);
		}
		sockaddr_in ret = {};
		std::memcpy(&ret, infos->ai_addr, sizeof(sockaddr_in));
		freeaddrinfo(infos);
		return ret;
	}
};

automaton_type automatonFromArg(std::string_view arg) {
	auto [prefix, sep, suffix] = partition(arg, ':');
	if (sep.empty())
		return *known_gadget(arg, automaton_type::alphabet_size_v);
	if (prefix == "file") {
		automaton_type thing{*deserialize(suffix)};
		automaton_type copy(thing);
		canonicalize(copy, copy.active_alphabet_size(), true);
		if (copy != thing)
			fmt::print("warning: canonicalizing changed {}", suffix);
		return copy;
	}
	throw std::runtime_error(fmt::format("bad prefix {} in {}", prefix, arg));
}

int main(int argc, char* argv[]) { //genbuild entrypoint
	std::vector<std::string> hosts;
	if (const char* nodes = std::getenv("SLURM_STEP_NODELIST")) {
		boost::process::ipstream pipe;
		std::string command = fmt::format("scontrol show hostnames {}", nodes);
		boost::process::child child(command, boost::process::std_out > pipe);
		std::string line;
		while (pipe && std::getline(pipe, line) && !line.empty())
			hosts.push_back(line);
		child.wait();
	}

	vector<pair<std::string_view, automaton_type>> inputs, outputs;
	std::vector<std::string_view> input_tokens = split_view(argv[1], ','),
			output_tokens = split_view(argv[2], ',');
	for (auto name : input_tokens)
		inputs.emplace_back(name, automatonFromArg(name));
	for (auto name : output_tokens)
		outputs.emplace_back(name, automatonFromArg(name));

	unique_ptr<GenerationalSearch> gs;
	if (hosts.size() <= 1) {
		//running locally (inside or outside of SLURM doesn't matter)
		GenerationalSearch gs(inputs, outputs);
		for (int generation = 1; ; ++generation)
			gs.advance();
	} else {
		DistributedGenerationalSearch gs(inputs, outputs, hosts);
		for (int generation = 1; ; ++generation)
			gs.advance();
	}

	return 0;
}