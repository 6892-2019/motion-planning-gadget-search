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
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <jemalloc/jemalloc.h>

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
typedef pair<automaton_type, Provenance> AutoProv;
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
	PageHolder(std::size_t desiredPageSize) : pageSize_(nallocx(desiredPageSize, 0)) {
		allocate();
	}
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
private:
	//should maybe be a small_vector?
	std::vector<std::unique_ptr<std::byte, free_deleter>> pages_;
	std::size_t pageSize_;
};

struct Finisher {
	Finisher(const ClosedSet* closed) : pages(16*1024*1024), cur(pages.allocate()), globalClosed(closed) {}
	Finisher(const Finisher& f, tbb::split) : pages(16*1024*1024), cur(pages.allocate()), globalClosed(f.globalClosed) {}
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
		}

//		Stopwatch stopwatch;
//		auto oldbytes = bytesAdopted;
//		auto oldcount = nextgen.size();

		//If we're globally pruning, we did it already.
		assert(((bool)globalClosed) == ((bool)rhs.globalClosed));
		for (PackProv& p : rhs.nextgen) {
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
		}
		globalClosedPruned += rhs.globalClosedPruned;
		localClosedPruned += rhs.localClosedPruned;
		//deliberately don't merge bytesAdopted

//		Stopwatch::Result timing = stopwatch.elapsed();
//		fmt::print("Finisher::join took {}ms; left {} ({}), right {} ({}), now {} ({}).\n",
//				timing.millis(), oldcount, oldbytes, rhs.nextgen.size(), rhs.bytesAdopted,
//				nextgen.size(), bytesAdopted);
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

void connect_at(const automaton_type& a, std::uint32_t gadgetIndex, bool mirrored,
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
					(*finish)(std::move(op), Provenance(gadgetIndex, connectPoint, c, mirrored));
				}
			},
			indirect_join);
}

void connect(const automaton_type& a, std::uint32_t gadgetIndex, bool mirrored,
		unsigned int locations, Finisher& finisher) {
	parallel_reduce(tbb::blocked_range<unsigned int>(0, locations),
			maybe_owning_ptr<Finisher>(&finisher, false),
			indirect_split,
			[&](const tbb::blocked_range<unsigned int>& r, maybe_owning_ptr<Finisher>& finish) {
				for (unsigned int l = r.begin(); l < r.end(); ++l)
					connect_at(a, gadgetIndex, mirrored, locations, l, *finish);
			},
			indirect_join);
}

void combine(const automaton_type& la, uint32_t l, bool leftMirror, automaton_type::state_type leftLocations,
		const automaton_type& ra, uint32_t r, bool rightMirror, automaton_type::state_type rightLocations,
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
			finish(std::move(permuted), Provenance(l, ll, leftMirror, r, rotation, rightMirror));
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
	void advance() {
		Stopwatch stopwatch;
		do_combine();
		do_connect();
		Stopwatch::Result timing = stopwatch.elapsed();
		fmt::print("Finished generation {} in {} ({}); produced {}, closed size {}.\n",
				generation_, timing.hms(), timing.utilization(), curgen_.size(), closed_.size());
		++generation_;
		if (curgen_.empty()) std::exit(0);
	}
private:
	PageHolder pages_;
	std::byte* cur_;
	vector<const Pack*> curgen_; //non-owning, stored in pages_ and already in closed_
	vector<Provenance> provenance_;
	ClosedSet closed_;
	vector<Input> inputs_;
	vector<Target> targets_;
	unsigned int generation_ = 0;

	void do_combine() {
		Stopwatch stopwatch;
		Finisher finisher(nullptr); //We'll never hit in closed_ when combining.
		if (generation_ == 0) {
			assert(closed_.empty());
			//"combine against nothing" to get started.  This includes mirrored
			//inputs, but that's safe and not too wasteful.
			for (auto& i : inputs_)
				finisher(automaton_type{i.normal}, Provenance(i.index));
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
		append(finisher.nextgen);

		Stopwatch::Result timing = stopwatch.elapsed();
		//TODO: total size, summary stats of produced or the entire closed set?
		fmt::print("Finished combine {} in {} ({}); produced {}, pruned {}, closed size {}.\n",
				generation_, timing.hms(), timing.utilization(), curgen_.size(), finisher.localClosedPruned, closed_.size());
	}

	void combine_once(const Pack* source, index_type sourceIndex, Finisher& finishAction) {
		//from toggles.cpp's Combine::operator(); TODO: may want to reunify
		automaton_type unpacked;
		unpack(unpacked, source);
		automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
		for (const Input& i : inputs_) {
			if (leftLocations + i.active_alphabet_size > automaton_type::alphabet_size_v) continue;
			combine(unpacked, sourceIndex, false, leftLocations, i.normal, i.index, false, i.active_alphabet_size, i.useful_rotations, finishAction);
		}
	}

	void do_connect() {
		Stopwatch connectwatch;
		std::size_t newStart = 0;
		unsigned int subgeneration = 0;
		std::size_t totalProduced = 0, totalGlobalPruned = 0, totalLocalPruned = 0;
		while (curgen_.size() != newStart) {
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
							connect(inflated, sourceIndex, false, locations, *finish);
						}
					},
					[](const maybe_owning_ptr<Finisher>& lhs, const maybe_owning_ptr<Finisher>& rhs) {
						lhs->join(*rhs);
					});
			std::size_t produced = finisher.nextgen.size();
			newStart = append(finisher.nextgen);
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

	std::size_t append(std::vector<PackProv>& next) {
//		Stopwatch stopwatch;
//		std::size_t sizeConsidered = 0, sizeCommitted = 0;
//		auto oldClosedSize = closed_.size();

		auto newStart = curgen_.size();
		for (PackProv& p : next) {
			auto size = packed_size(p.first);
//			sizeConsidered += size;
			auto hash = packed_hash(p.first);
			//If we're globally pruning in Finisher, this should always succeed.
			if (!closed_.count(p.first, hash)) {
				assert(size < pages_.page_size());
				for (const Target& t : targets_)
					if (t.packed_hash == hash || t.mirror_packed_hash == hash) {
						print_provenance_backtrace(p.second);
						//TODO: maybe put it in some member variable to be checked when convenient?
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
			//TODO: we could reduce peak memory by freeing pages from the
			//Finisher feeding us after we're done copying off of them.
		}

//		Stopwatch::Result timing = stopwatch.elapsed();
//		fmt::print("append took {}ms; curgen {} -> {}, closed {} -> {}; considered {} ({}), committed {} ({}), ratio {} ({}).\n",
//				timing.millis(), newStart, curgen_.size(), oldClosedSize, closed_.size(),
//				next.size(), sizeConsidered, closed_.size() - oldClosedSize, sizeCommitted,
//				((double)(closed_.size() - oldClosedSize))/next.size(), ((double)sizeCommitted)/sizeConsidered);
		return newStart;
	}

	[[gnu::cold]]
	void print_provenance_backtrace(Provenance& provenance) {
		circular_deque<std::uint32_t, 32> queue;
		linear_set<std::uint32_t> printed;

		std::cout << "<found> = " << provenance << '\n';
		for (auto p : provenance.parents())
			if (printed.insert(p).second)
				queue.push_back(p);

		while (!queue.empty()) {
			auto idx = queue.pop_front();
			auto prov = provenance_.at(idx);
			std::cout << idx << " = " << prov << '\n';
			//Ideally we'd print the automaton here, but we're no longer
			//maintaining an id->automaton map.  We'll have to replay the
			//log this code is printing out.
			for (auto p : prov.parents())
				if (printed.insert(p).second)
					queue.push_back(p);
		}

		std::cout << std::flush;
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
	vector<pair<std::string_view, automaton_type>> inputs, outputs;

	std::vector<std::string_view> input_tokens = split_view(argv[1], ','),
			output_tokens = split_view(argv[2], ',');
	for (auto name : input_tokens)
		inputs.emplace_back(name, automatonFromArg(name));
	for (auto name : output_tokens)
		outputs.emplace_back(name, automatonFromArg(name));

	GenerationalSearch gs(inputs, outputs);
	for (int generation = 1; ; ++generation)
		gs.advance();

	return 0;
}