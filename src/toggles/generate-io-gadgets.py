#!/usr/bin/env python3

import sys, argparse
import itertools
import functools
import json
from types import SimpleNamespace
from pathlib import Path

def bracelets(size):
    u = set()
    for i in range(2**size):
        candidates = []
        x = [bool(i & (1 << p)) for p in range(size)]
        for r in range(size):
            candidates.append(x[r:] + x[:r])
        x.reverse()
        for r in range(size):
            candidates.append(x[r:] + x[:r])
        u.add(tuple(min(candidates)))
    return u


# Generate fanin/fanout gadgets.  These are named by degree and have port 0 as the operative port (so a 3-fanin has
# edges 1 -> 0 and 2 -> 0).  The 2-fanin and 2-fanout are both the diode, so we start at 3.
def generate_faninout(args):
    finished = {}
    for degree in range(3, 17):
        finished['{}-fanin'.format(degree)] = {
            'dedges': [[0, i, 0, 0] for i in range(1, degree)],
            'inputs': list(range(1, degree)),
            'outputs': [0],
        }
        finished['{}-fanout'.format(degree)] = {
            'dedges': [[0, 0, i, 0] for i in range(1, degree)],
            'inputs': [0],
            'outputs': list(range(1, degree)),
        }

    gadgets = {'gadgets': finished}
    filename = args.output_dir / 'fanin-fanout.json'
    with open(filename, 'w') as f:
        json.dump(gadgets, f, check_circular=False, separators=(',', ':'))


# Generate various "mixing" gadgets using bracelets, e.g., 0,1,3-in-2,4-out.  The only 2-mixer is the diode and the
# 3-mixers are the 3-fanin and 3-fanout, respectively.  To avoid the fanin/fanout gadgets, we require that neither the
# input set nor the output set are the singleton {0}.
def generate_mixers(args):
    finished = {}
    for locations in range(4, 17):
        configurations = []
        for ports in bracelets(locations):
            inputs, outputs = [i for i in range(len(ports)) if not ports[i]], [i for i in range(len(ports)) if ports[i]]
            if len(inputs) < 2 or len(outputs) < 2: continue
            configurations.append((tuple(inputs), tuple(outputs)))
        configurations.sort(key=lambda x: (len(x[0]), x[0]))
        for c in configurations:
            name = '{}-in-{}-out'.format(','.join(map(str, c[0])), ','.join(map(str, c[1])))
            finished[name] = {
                'dedges': [[0, i, j, 0] for i in c[0] for j in c[1]],
                'inputs': c[0],
                'outputs': c[1],
            }

    gadgets = {'gadgets': finished}
    filename = args.output_dir / 'mixers.json'
    with open(filename, 'w') as f:
        json.dump(gadgets, f, check_circular=False, separators=(',', ':'))


SAME = sys.intern('same')
DIFFERENT = sys.intern('different')
all_tunnels = (
    #TODO: the SAME wire is a diode; we could include that if we wanted
    # setdown is a disemitripwire; setup is the other disemitripwire (setting state to 0)
    SimpleNamespace(name='diupwire', states=1, outputs=1, active=True, polarity='up',
        edges={0: ((0, 1, 0),)}),
    SimpleNamespace(name='didownwire', states=1, outputs=1, active=True, polarity='down',
        edges={0: ((0, 1, 1),)}),
    SimpleNamespace(name='ditripwire', states=1, outputs=1, active=True, polarity=None,
        edges={0: ((0, 1, DIFFERENT),)}),

    SimpleNamespace(name='point', states=2, outputs=2, active=False, polarity=None,
        edges={0: ((0, 1, SAME),), 1: ((0, 2, SAME),)}),
    SimpleNamespace(name='uppoint', states=2, outputs=2, active=True, polarity='up',
        edges={0: ((0, 1, 0),), 1: ((0, 2, 0),)}),
    SimpleNamespace(name='downpoint', states=2, outputs=2, active=True, polarity='down',
        edges={0: ((0, 1, 1),), 1: ((0, 2, 1),)}),
    SimpleNamespace(name='trippoint', states=2, outputs=2, active=True, polarity=None,
        edges={0: ((0, 1, DIFFERENT),), 1: ((0, 2, DIFFERENT),)}),
)

def name_for_state(state_sig, state):
    statechunks = [('up', 'down')[s] for i, s in enumerate(state) if state_sig[i] == 2]
    return '-'.join(statechunks)

@functools.cache
def state_data(state_sig):
    # We have to force this because we're going to iterate it repeatedly.
    states = list(itertools.product(*[range(s) for s in state_sig]))
    state_num = {s: i for i, s in enumerate(states)}
    state_names = {i: name_for_state(state_sig, s) for i, s in enumerate(states)}
    next_states = {s: {
                0: tuple(0 for _ in s),
                1: tuple(1 if state_sig[i] == 2 else 0 for i, p in enumerate(s)),
                SAME: tuple(s),
                DIFFERENT: tuple(1-p if state_sig[i] == 2 else 0 for i, p in enumerate(s)),
            } for s in states}
    return states, state_num, state_names, next_states


def generate_input_outputs0(args, locations: int):
    finished = {}
    configurations = []
    for ports in bracelets(locations):
        inputs, outputs = [i for i in range(len(ports)) if not ports[i]], [i for i in range(len(ports)) if ports[i]]
        # A single input would just be some kind of point.  A single output is just fan-in.
        if len(inputs) < 2 or len(outputs) < 2: continue
        # An input can cover at most two uncovered outputs.
        if 2 * len(inputs) < len(outputs): continue
        configurations.append((tuple(inputs), tuple(outputs)))
    configurations.sort(key=lambda x: (len(x[0]), x[0]))

    for inputs, outputs in configurations:
        one_perms = [(o,) for o in outputs]
        two_perms = list(itertools.permutations(outputs, 2))
        for tunnels in itertools.combinations_with_replacement(all_tunnels, len(inputs)):
            # must have at least one two-state tunnel
            if not any(t for t in tunnels if t.states == 2): continue
            # can't use down polarity before having used up polarity
            if next((t.polarity for t in tunnels if t.polarity), None) == 'down': continue
            # must cover the outputs
            total_output_locations = sum(t.outputs for t in tunnels)
            if total_output_locations < len(outputs): continue
            # must have the potential to change state in some state (i.e., not all points)
            if not any(t.active for t in tunnels): continue

            states_sig = tuple(t.states for t in tunnels)
            states, state_num, state_names, next_states = state_data(states_sig)
            # TODO: we could map all the state numbers once, and then remap just
            # locations in the target assignment loop below; this would also save
            # calculating the nontrivial states

            for targets in itertools.product(*[two_perms if t.outputs == 2 else one_perms for t in tunnels]):
                # We checked earlier that we *can* cover the outputs; now we're
                # checking that we actually did.
                if len(set(itertools.chain(*targets))) < len(outputs): continue

                edges = []
                for si, s in enumerate(states):
                    next_state = next_states[s]
                    for ti, t in enumerate(tunnels):
                        loc_map = (inputs[ti], *targets[ti])
                        edges.extend(
                            [(si, loc_map[b], loc_map[c], state_num[next_state[d]]) for b, c, d in t.edges[s[ti]]])
                edges.sort()

                our_state_names = state_names
                nontrivial_states = {s for s, x, y, t in edges if s != t}
                if len(nontrivial_states) != len(state_names):
                    our_state_names = {k: v for k, v in state_names.items() if k in nontrivial_states}
                if not our_state_names: continue

                namechunks = []
                for i, t in enumerate(tunnels):
                    if len(targets[i]) == 1:
                        namechunks.append('{}{}-{}'.format(inputs[i], targets[i][0], t.name))
                    elif len(targets[i]) == 2:
                        namechunks.append('{}{}{}-{}'.format(inputs[i], targets[i][0], targets[i][1], t.name))
                    else:
                        raise ValueError('future?')
                name = '-'.join(namechunks)
                finished[name] = {
                    'dedges': edges,
                    'state-names': our_state_names,
                    'pragma': 'allow-pruning-named-states',
                    'inputs': inputs,
                    'outputs': outputs,
                    # TODO: planar key (need to figure out how to check planarity here; it isn't as simple as for doors)
                }

    gadgets = {'gadgets': finished}
    filename = args.output_dir / 'input-output-{}.json'.format(locations)
    with open(filename, 'w') as f:
        json.dump(gadgets, f, check_circular=False, separators=(',', ':'))


def generate_input_outputs(args):
    for locations in range(4, args.max_locations+1):
        generate_input_outputs0(args, locations)

def main(args):
    generate_faninout(args)
    generate_mixers(args)
    generate_input_outputs(args)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--max-locations', type=int, default=7)
    parser.add_argument('output_dir', type=Path)
    args = parser.parse_args()
    if not args.output_dir.is_dir():
        print(args.output_dir, 'does not exist or is not a directory')
        sys.exit(1)
    main(args)