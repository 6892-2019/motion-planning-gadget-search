/*
 * File:   canonicalize.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on April 15, 2017, 9:37 PM
 */

#ifndef CANONICALIZE_HPP
#define CANONICALIZE_HPP

#include "automaton.hpp"
#include <nausparse.h>

template<unsigned int N>
void canonicalize(automaton::Automaton<N>& a, unsigned int locations) {
	sparsegraph sg, canon;
	SG_INIT(sg);
	sg.nv = 0;
	sg.nde = 0;
	SG_INIT(canon);
	canon.nv = 0;
	canon.nde = 0;

	using state_type = automaton::AutomatonBase::state_type;
	using symbol_type = automaton::AutomatonBase::symbol_type;
	//TODO: these could just be functions
	std::unordered_map<std::pair<state_type, unsigned int>, int,
		boost::hash<std::pair<state_type, unsigned int>>> towers;
	std::unordered_map<unsigned int, int> edgecolors;
	std::unordered_map<unsigned int, int> towercolors;
	for (state_type s = 0; s < a.size(); ++s)
		for (unsigned int l = 0; l < locations; ++l)
			towers[{s, l}] = sg.nv++;
	for (unsigned int l = 0; l < locations; ++l)
		edgecolors[l] = sg.nv++;
	for (unsigned int t = 0; t < a.size(); ++t)
		towercolors[t] = sg.nv++;
	sg.nde = 2 * towers.size() //undirected cycle through each tower
			+ 2 * edgecolors.size() //undirected cycle through the colors
			+ 2 * a.size() * edgecolors.size() //undirected edge between each color and each node in its level
			+ 2 * towercolors.size() * edgecolors.size() //undirected edge between each color and each node in its tower
			+ a.edges();
	SG_ALLOC(sg, sg.nv, sg.nde, "asdf");

	auto incr = [&](symbol_type s){return s == locations-1 ? 0 : s+1;};
	auto decr = [&](symbol_type s){return s == 0 ? locations-1 : s-1;};

	int ei = 0;
	for (state_type s = 0; s < a.size(); ++s)
		for (unsigned int l = 0; l < locations; ++l) {
			int vi = towers.at({s, l});
			sg.v[vi] = ei;

			//below/above in the tower
			sg.e[ei++] = towers.at({s, decr(l)});
			sg.e[ei++] = towers.at({s, incr(l)});

			sg.e[ei++] = edgecolors.at(l);
			sg.e[ei++] = towercolors.at(s);

			auto dests = a.step(s, l);
			assert(dests.size() <= 1 && "should already be deterministic");
			if (!dests.empty())
				sg.e[ei++] = towers.at({dests.front(), l});

			sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
		}

	for (unsigned int l = 0; l < locations; ++l) {
		int vi = edgecolors.at(l);
		sg.v[vi] = ei;
		sg.e[ei++] = edgecolors.at(decr(l));
		sg.e[ei++] = edgecolors.at(incr(l));
		for (state_type s = 0; s < a.size(); ++s)
			sg.e[ei++] = towers.at({s, l});
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}

	for (unsigned int t = 0; t < a.size(); ++t) {
		int vi = towercolors.at(t);
		sg.v[vi] = ei;
		for (symbol_type l = 0; l < locations; ++l)
			sg.e[ei++] = towers.at({t, l});
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}
	assert(ei == sg.nde && "wrong number of edges");

	dynarray<int> lab(sg.nv), ptn(sg.nv);
	std::iota(lab.begin(), lab.end(), 0);
	std::fill(ptn.begin(), ptn.end(), 1); //counter-intuitively, partitions end at 0
	ptn[towers.size()-1] = 0;
	ptn[towers.size()+edgecolors.size()-1] = 0;
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

	dynarray<state_type> stateinvperm(a.size());
	std::iota(stateinvperm.begin(), stateinvperm.end(), 0);
	std::sort(stateinvperm.begin(), stateinvperm.end(), [&](auto l, auto r){return lab[towercolors.at(l)] < lab[towercolors.at(r)];});
	dynarray<state_type> stateperm(a.size());
	for (state_type i = 0; i < stateperm.size(); ++i)
		stateperm[stateinvperm[i]] = i;
	//state 0 is the initial state, we can't renumber it
	std::iter_swap(stateperm.begin(), std::find(stateperm.begin(), stateperm.end(), 0));

	//Locations are intrinsically ordered; we use the labeling to select a
	//start point and a direction, then we walk the cycle ourselves.
	const symbol_type minloc = *std::min_element(boost::counting_iterator<symbol_type>(0),
		boost::counting_iterator<symbol_type>(locations),
		[&](auto l, auto r){return lab[edgecolors.at(l)] < lab[edgecolors.at(r)];});
	dynarray<symbol_type> locationperm(automaton::Automaton<N>::alphabet_size_v);
	bool cycleDown = lab[edgecolors.at(decr(minloc))] < lab[edgecolors.at(incr(minloc))];
	for (unsigned int i = 0, loc = minloc; i < locations; ++i, loc = cycleDown ? decr(loc) : incr(loc))
		locationperm[i] = loc;
	std::fill(locationperm.begin()+locations, locationperm.end(), std::numeric_limits<symbol_type>::max());

	a.renumber(stateperm.begin(), locationperm.begin());
	a.prepareForEquals();
}

#endif /* CANONICALIZE_HPP */

