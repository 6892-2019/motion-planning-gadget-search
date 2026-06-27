#!/usr/bin/env python3

from typing import Optional, List, Tuple, Dict
import graphviz
from gadget import Gadget
import math
import os
import subprocess

def visualize_gadget(gadget: Gadget, output_path: str,
                    format: str = 'png', show_labels: bool = True,
                    state_labels: Optional[Dict[int, str]] = None,
                    loc_labels: Optional[Dict[int, str]] = None) -> None:
    """
    Visualize a gadget using graphviz.

    Args:
        gadget: The gadget to visualize
        output_path: Path where the visualization should be saved (without extension)
        format: Output format ('png', 'svg', 'pdf', etc.)
        show_labels: Whether to show state and location labels
        state_labels: Optional display names for states, e.g. {0: 'open', 1: 'closed'}
        loc_labels: Optional display names for locations, e.g. {0: 'in', 1: 'out'}
    """
    def fmt_state(s: int) -> str:
        if state_labels and s in state_labels:
            return state_labels[s]
        return f'S{s}'

    def fmt_loc(l: int) -> str:
        if loc_labels and l in loc_labels:
            return loc_labels[l]
        return f'Location {l}'

    # Create a new directed graph
    dot = graphviz.Digraph(comment='Gadget Visualization')
    dot.attr(rankdir='LR')  # Left to right layout

    # Create subgraphs for each location to align states vertically
    for loc in range(gadget.num_locations):
        with dot.subgraph(name=f'cluster_loc_{loc}') as c:
            c.attr(label=fmt_loc(loc))
            # Add nodes for each state at this location
            for state in range(gadget.num_states):
                node_name = f's{state}l{loc}'
                label = fmt_state(state) if show_labels else ''
                c.node(node_name, label, shape='circle')
    
    # Add edges for transitions
    for from_state, from_loc, to_state, to_loc in gadget.transitions:
        from_node = f's{from_state}l{from_loc}'
        to_node = f's{to_state}l{to_loc}'
        
        # Use different styles for reversible vs one-way transitions
        if gadget.reversible:
            dot.edge(from_node, to_node, dir='both')
        else:
            # Check if reverse transition exists
            reverse_exists = (to_state, to_loc, from_state, from_loc) in gadget.transitions
            if reverse_exists:
                dot.edge(from_node, to_node, dir='both')
            else:
                dot.edge(from_node, to_node)
    
    # Render the graph
    dot.render(output_path, format=format, cleanup=True)

def visualize_gadget_comparison(gadget1: Gadget, gadget2: Gadget,
                              output_path: str, format: str = 'png',
                              show_labels: bool = True,
                              state_labels: Optional[Dict[int, str]] = None,
                              loc_labels: Optional[Dict[int, str]] = None) -> None:
    """
    Create a side-by-side visualization of two gadgets for comparison.

    Args:
        gadget1: First gadget to visualize
        gadget2: Second gadget to visualize
        output_path: Path where the visualization should be saved (without extension)
        format: Output format ('png', 'svg', 'pdf', etc.)
        show_labels: Whether to show state and location labels
        state_labels: Optional display names for states, e.g. {0: 'open', 1: 'closed'}
        loc_labels: Optional display names for locations, e.g. {0: 'in', 1: 'out'}
    """
    def fmt_state(s: int) -> str:
        if state_labels and s in state_labels:
            return state_labels[s]
        return f'S{s}'

    def fmt_loc(l: int) -> str:
        if loc_labels and l in loc_labels:
            return loc_labels[l]
        return f'Location {l}'

    dot = graphviz.Digraph(comment='Gadget Comparison')
    dot.attr(rankdir='LR')

    # Create two clusters for the gadgets
    for idx, gadget in enumerate([gadget1, gadget2]):
        with dot.subgraph(name=f'cluster_gadget_{idx}') as c:
            c.attr(label=f'Gadget {idx+1}')

            # Create location subgraphs
            for loc in range(gadget.num_locations):
                with c.subgraph(name=f'cluster_g{idx}_loc_{loc}') as loc_c:
                    loc_c.attr(label=fmt_loc(loc))
                    # Add nodes for each state at this location
                    for state in range(gadget.num_states):
                        node_name = f'g{idx}_s{state}l{loc}'
                        label = fmt_state(state) if show_labels else ''
                        loc_c.node(node_name, label, shape='circle')

            # Add edges for transitions
            for from_state, from_loc, to_state, to_loc in gadget.transitions:
                from_node = f'g{idx}_s{from_state}l{from_loc}'
                to_node = f'g{idx}_s{to_state}l{to_loc}'

                if gadget.reversible:
                    c.edge(from_node, to_node, dir='both')
                else:
                    reverse_exists = (to_state, to_loc, from_state, from_loc) in gadget.transitions
                    if reverse_exists:
                        c.edge(from_node, to_node, dir='both')
                    else:
                        c.edge(from_node, to_node)
    
    # Render the graph
    dot.render(output_path, format=format, cleanup=True)

def visualize_gadget_circular(gadget: Gadget, output_path: str,
                            format: str = 'png', show_labels: bool = True,
                            state_labels: Optional[Dict[int, str]] = None,
                            loc_labels: Optional[Dict[int, str]] = None) -> None:
    """
    Visualize a gadget using a circular layout where:
    - Each state is a large empty circle with its label above
    - States are arranged with smaller numbers to the left
    - Locations are dots arranged on the state circle's perimeter
    - Transitions are arrows between location dots within each state circle
    - Arrows are labeled with their destination states

    Args:
        gadget: The gadget to visualize
        output_path: Path where the visualization should be saved (without extension)
        format: Output format ('png', 'svg', 'pdf', etc.)
        show_labels: Whether to show state and location labels
        state_labels: Optional display names for states, e.g. {0: 'open', 1: 'closed'}
        loc_labels: Optional display names for locations, e.g. {0: 'in', 1: 'out'}
    """
    def fmt_state(s: int) -> str:
        if state_labels and s in state_labels:
            return state_labels[s]
        return f'State {s}'

    def fmt_loc(l: int) -> str:
        if loc_labels and l in loc_labels:
            return loc_labels[l]
        return str(l)

    def fmt_state_arrow(s: int) -> str:
        if state_labels and s in state_labels:
            return f'→{state_labels[s]}'
        return f'→{s}'

    dot = graphviz.Digraph(comment='Gadget Visualization (Circular)')
    dot.attr(rankdir='LR')
    
    # Calculate positions for each state's circle and its location dots
    base_radius = 1.5  # Base radius for arranging states in a circle
    state_circle_radius = 0.6  # Radius of each state's circle
    
    # Adjust spacing based on number of states and locations
    # More states or locations need more space to prevent overlap
    spacing_multiplier = max(1.0, gadget.num_states * 0.5)
    radius = base_radius * spacing_multiplier
    
    # Create nodes for state circles
    for state in range(gadget.num_states):
        # Position state circle, with smaller states to the left
        # Map state number to x position linearly from -radius to radius
        x = -radius + (2 * radius * state / (gadget.num_states - 1 if gadget.num_states > 1 else 1))
        y = 0  # All states at same y level
        
        # Create cluster for state and its locations
        with dot.subgraph(name=f'cluster_state_{state}') as c:
            # Add state label above the circle
            label_y_offset = state_circle_radius + 0.2  # Offset for label placement
            c.node(f's{state}_label', fmt_state(state) if show_labels else '',
                  pos=f"{x},{y + label_y_offset}!", shape='none')

            # Create an empty circle for the state
            c.node(f's{state}', '', pos=f"{x},{y}!",
                  shape='circle', width=str(2*state_circle_radius),
                  height=str(2*state_circle_radius), style='', color='black')

            # Create location dots exactly on the state circle
            for loc in range(gadget.num_locations):
                # Adjust starting angle to have first location at top
                loc_angle = (2 * math.pi * loc / gadget.num_locations) - (math.pi / 2)
                # Place dots exactly on the circle's perimeter
                loc_x = x + state_circle_radius * math.cos(loc_angle)
                loc_y = y + state_circle_radius * math.sin(loc_angle)

                # Create location dot
                node_name = f's{state}l{loc}'
                label = fmt_loc(loc) if show_labels else ''
                # Offset the location label slightly outward
                if show_labels:
                    label_offset = 0.15  # Increased offset for better visibility
                    label_x = x + (state_circle_radius + label_offset) * math.cos(loc_angle)
                    label_y = y + (state_circle_radius + label_offset) * math.sin(loc_angle)
                    c.node(f'{node_name}_label', label, pos=f"{label_x},{label_y}!", shape='none')
                c.node(node_name, '', pos=f"{loc_x},{loc_y}!", shape='point', width='0.1')

            # Add transitions within this state's circle
            for from_loc in range(gadget.num_locations):
                for to_loc in range(gadget.num_locations):
                    # Check if there's a transition to any other state from these locations
                    for to_state in range(gadget.num_states):
                        if (state, from_loc, to_state, to_loc) in gadget.transitions:
                            from_node = f's{state}l{from_loc}'
                            to_node = f's{state}l{to_loc}'

                            # Label with destination state
                            c.edge(from_node, to_node, label=fmt_state_arrow(to_state))

                            # For reversible gadgets, add the reverse transition with its own label
                            if gadget.reversible and (to_state, to_loc, state, from_loc) not in gadget.transitions:
                                c.edge(to_node, from_node, label=fmt_state_arrow(state))

                        # For non-reversible gadgets, check explicit reverse transitions
                        elif not gadget.reversible and (to_state, to_loc, state, from_loc) in gadget.transitions:
                            # Add the explicit reverse transition
                            from_node = f's{state}l{to_loc}'
                            to_node = f's{state}l{from_loc}'
                            c.edge(from_node, to_node, label=fmt_state_arrow(state))

    # Use neato layout engine for precise node positioning
    dot.attr(layout='neato')

    # Render the graph
    dot.render(output_path, format=format, cleanup=True)


def visualize_gadget_automaton(gadget: Gadget, output_path: str,
                               format: str = 'png',
                               state_labels: Optional[Dict[int, str]] = None,
                               loc_labels: Optional[Dict[int, str]] = None,
                               title: str = '') -> None:
    """
    Visualize a gadget as a state-machine (automaton) diagram in the style
    commonly used in motion-planning gadget papers.

    States are nodes; each transition (from_state, entry_loc) → (to_state, exit_loc)
    is a directed edge from the from-state node to the to-state node, labelled
    "entry_loc → exit_loc".  Multiple transitions between the same pair of states
    are drawn as parallel edges with their respective port labels.

    This layout is easier to cross-check against traversal-mode tables than the
    circular perimeter layout.

    Args:
        gadget: The gadget to visualize.
        output_path: Output file path without extension.
        format: Graphviz output format ('png', 'svg', 'pdf', …).
        state_labels: Display names for states, e.g. {0: 'open', 1: 'closed'}.
        loc_labels: Display names for locations, e.g. {0: 'in', 1: 'out'}.
        title: Optional graph title shown as a label.
    """
    def fs(s: int) -> str:
        return state_labels[s] if state_labels and s in state_labels else f'S{s}'

    def fl(l: int) -> str:
        return loc_labels[l] if loc_labels and l in loc_labels else str(l)

    dot = graphviz.Digraph(comment='Gadget automaton')
    dot.attr(rankdir='LR', fontsize='12')
    if title:
        dot.attr(label=title, labelloc='t')

    for state in range(gadget.num_states):
        dot.node(str(state), fs(state), shape='circle',
                 width='0.7', fixedsize='true')

    # One directed edge per transition, labelled "entry→exit".
    # Parallel edges between the same pair of states naturally appear as
    # distinct arrows, mirroring distinct rows in the traversal-mode table.
    for from_s, from_l, to_s, to_l in sorted(gadget.transitions):
        dot.edge(str(from_s), str(to_s),
                 label=f'{fl(from_l)}→{fl(to_l)}',
                 dir='forward')

    dot.render(output_path, format=format, cleanup=True)


# ── Clean diagrams for verifying constructions ───────────────────────────────

def visualize_state_diagram(gadget: Gadget, output_path: str,
                            format: str = 'png',
                            state_labels: Optional[Dict[int, str]] = None,
                            loc_labels: Optional[Dict[int, str]] = None,
                            title: str = '') -> None:
    """
    Clean STATE diagram: one node per state, one edge per traversal labelled
    `entry→exit`.  A traversal whose exact reverse is also present is drawn once
    as a bidirectional edge `entry⇄exit`, so reversible gadgets read cleanly.
    Reversibility is detected from the transitions, not the construction flag.
    """
    def fs(s: int) -> str:
        return state_labels[s] if state_labels and s in state_labels else f'S{s}'

    def fl(l: int) -> str:
        return loc_labels[l] if loc_labels and l in loc_labels else str(l)

    T = set(gadget.transitions)
    dot = graphviz.Digraph(comment='State diagram')
    dot.attr(rankdir='LR', fontsize='12')
    if title:
        dot.attr(label=title, labelloc='t')

    for s in range(gadget.num_states):
        dot.node(str(s), fs(s), shape='circle', width='0.7', fixedsize='true')

    drawn: set = set()
    for from_s, from_l, to_s, to_l in sorted(gadget.transitions):
        if (from_s, from_l, to_s, to_l) in drawn:
            continue
        reverse = (to_s, to_l, from_s, from_l)
        if reverse in T:
            dot.edge(str(from_s), str(to_s),
                     label=f'{fl(from_l)}⇄{fl(to_l)}', dir='both',
                     color='#1f6feb', fontcolor='#1f6feb')
            drawn.add(reverse)
        else:
            dot.edge(str(from_s), str(to_s), label=f'{fl(from_l)}→{fl(to_l)}')
        drawn.add((from_s, from_l, to_s, to_l))

    dot.render(output_path, format=format, cleanup=True)


def visualize_state_location_diagram(gadget: Gadget, output_path: str,
                                     format: str = 'png',
                                     state_labels: Optional[Dict[int, str]] = None,
                                     loc_labels: Optional[Dict[int, str]] = None,
                                     title: str = '') -> None:
    """
    STATE-LOCATION (configuration) diagram: the directed graph the agent
    actually walks.  Vertices are (state, location) pairs grouped in a box per
    state; an edge goes from (s, entry) to (s', exit) for each transition.
    Reverse pairs are merged into one bidirectional edge.

    This is the most faithful picture for hand-checking traversals: a traversal
    mode (s, a) → (s', b) is exactly one arrow.
    """
    def fs(s: int) -> str:
        return state_labels[s] if state_labels and s in state_labels else f'S{s}'

    def fl(l: int) -> str:
        return loc_labels[l] if loc_labels and l in loc_labels else str(l)

    T = set(gadget.transitions)
    dot = graphviz.Digraph(comment='State-location diagram')
    dot.attr(rankdir='LR', fontsize='12', compound='true')
    if title:
        dot.attr(label=title, labelloc='t')

    def node_id(s: int, l: int) -> str:
        return f's{s}l{l}'

    for s in range(gadget.num_states):
        with dot.subgraph(name=f'cluster_state_{s}') as c:
            c.attr(label=fs(s), style='rounded', color='#999999')
            for l in range(gadget.num_locations):
                c.node(node_id(s, l), fl(l), shape='circle',
                       width='0.45', fixedsize='true', fontsize='10')

    drawn: set = set()
    for from_s, from_l, to_s, to_l in sorted(gadget.transitions):
        if (from_s, from_l, to_s, to_l) in drawn:
            continue
        reverse = (to_s, to_l, from_s, from_l)
        if reverse in T:
            dot.edge(node_id(from_s, from_l), node_id(to_s, to_l), dir='both',
                     color='#1f6feb')
            drawn.add(reverse)
        else:
            dot.edge(node_id(from_s, from_l), node_id(to_s, to_l))
        drawn.add((from_s, from_l, to_s, to_l))

    dot.render(output_path, format=format, cleanup=True)


# ── Paper-style box diagrams (gadgets as boxes, ports on the perimeter) ──────
#
# These render gadgets the way the motion-planning gadget papers do: each gadget
# is a rounded box, its locations are ports on the boundary (in cyclic order),
# its transitions are arrows drawn *inside* the box (one line style per state),
# and in a system the connected ports are joined by wires.  Output is SVG
# (with exact geometry); a PNG copy is produced when a converter is available.

_STATE_COLORS = ['#1a1a1a', '#1f6feb', '#d4380d', '#389e0d', '#9333ea', '#b8860b']
_STATE_DASHES = ['', '7,4', '2,4', '9,4,2,4', '1,5', '6,3,1,3']


def _state_style(s: int) -> Tuple[str, str]:
    """(stroke color, dash array) for transitions out of state s."""
    return (_STATE_COLORS[s % len(_STATE_COLORS)],
            _STATE_DASHES[s % len(_STATE_DASHES)])


def _ray_to_rect(cx, cy, hw, hh, angle_deg):
    """
    Boundary point where a ray from the box centre at `angle_deg` (math
    convention, CCW, 0 = +x) meets the rounded box, with the outward unit
    normal of the edge it lands on.  Returns (px, py, nx, ny).
    """
    rad = math.radians(angle_deg)
    dx, dy = math.cos(rad), -math.sin(rad)          # SVG y grows downward
    tx = hw / abs(dx) if abs(dx) > 1e-9 else math.inf
    ty = hh / abs(dy) if abs(dy) > 1e-9 else math.inf
    if tx <= ty:
        px, py = cx + dx * tx, cy + dy * tx
        nx, ny = (1.0 if dx > 0 else -1.0), 0.0
    else:
        px, py = cx + dx * ty, cy + dy * ty
        nx, ny = 0.0, (1.0 if dy > 0 else -1.0)
    return px, py, nx, ny


def _port_geometry(cx, cy, hw, hh, num_locations, sides=None):
    """Map each location index to (px, py, nx, ny) on the box perimeter.

    With `sides` (a dict loc -> 'L'/'R'/'T'/'B'), ports are placed on the named
    edges, evenly spaced in index order along each edge — this matches the
    papers' tunnel layout (tunnel endpoints opposite each other on L/R).

    Without `sides`, location 0 sits at the left edge and successive locations
    proceed counter-clockwise around the boundary, so the cyclic order is
    visible (a 2-port tunnel becomes left/right).
    """
    if sides is None:
        geo = {}
        for i in range(num_locations):
            angle = 180.0 - (360.0 * i / max(1, num_locations))
            geo[i] = _ray_to_rect(cx, cy, hw, hh, angle)
        return geo

    by_side = {'L': [], 'R': [], 'T': [], 'B': []}
    for loc in range(num_locations):
        by_side[sides.get(loc, 'L')].append(loc)
    geo = {}
    for side, locs in by_side.items():
        for j, loc in enumerate(locs):
            frac = (j + 1) / (len(locs) + 1)
            if side == 'L':
                geo[loc] = (cx - hw, cy - hh + 2 * hh * frac, -1.0, 0.0)
            elif side == 'R':
                geo[loc] = (cx + hw, cy - hh + 2 * hh * frac, 1.0, 0.0)
            elif side == 'T':
                geo[loc] = (cx - hw + 2 * hw * frac, cy - hh, 0.0, -1.0)
            else:  # 'B'
                geo[loc] = (cx - hw + 2 * hw * frac, cy + hh, 0.0, 1.0)
    return geo


def _arrow_path(p0, p1, perp=None, bow=0.0):
    """Quadratic-Bézier path from p0 to p1, bowed by `bow` along `perp`.

    If `perp` is None it is derived from the p0→p1 direction; pass an explicit
    perpendicular to keep the bow on a consistent screen side for both
    directions of a tunnel.
    """
    mx, my = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2
    if perp is None:
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        length = math.hypot(dx, dy) or 1.0
        perp = (-dy / length, dx / length)
    cx, cy = mx + perp[0] * bow, my + perp[1] * bow
    return f'M {p0[0]:.1f} {p0[1]:.1f} Q {cx:.1f} {cy:.1f} {p1[0]:.1f} {p1[1]:.1f}', (cx, cy)


def _arrowhead(tip, dirx, diry, color, size=10.0):
    """A filled triangular arrowhead at `tip` pointing along (dirx, diry).
    Hand-drawn (not an SVG marker) so it renders in any rasteriser."""
    length = math.hypot(dirx, diry) or 1.0
    ux, uy = dirx / length, diry / length
    bx, by = tip[0] - ux * size, tip[1] - uy * size
    px, py = -uy, ux
    w = size * 0.55
    return (f'<polygon points="{tip[0]:.1f},{tip[1]:.1f} '
            f'{bx+px*w:.1f},{by+py*w:.1f} {bx-px*w:.1f},{by-py*w:.1f}" '
            f'fill="{color}"/>')


def _label(x, y, text, color):
    """Text with an opaque rounded background so it stays legible over arcs."""
    w = len(text) * 6.8 + 8
    return (f'<rect x="{x-w/2:.1f}" y="{y-9:.1f}" width="{w:.1f}" height="17" '
            f'rx="4" fill="white" fill-opacity="0.88"/>'
            f'<text x="{x:.1f}" y="{y+4:.1f}" text-anchor="middle" '
            f'font-size="10" fill="{color}" font-family="sans-serif">{text}</text>')


def _wire_path(p, np_, q, nq, k=46.0):
    """Cubic-Bézier path that leaves p and arrives at q perpendicular to each box."""
    c1 = (p[0] + np_[0] * k, p[1] + np_[1] * k)
    c2 = (q[0] + nq[0] * k, q[1] + nq[1] * k)
    return (f'M {p[0]:.1f} {p[1]:.1f} C {c1[0]:.1f} {c1[1]:.1f} '
            f'{c2[0]:.1f} {c2[1]:.1f} {q[0]:.1f} {q[1]:.1f}')


def _svg_to_png(svg_text: str, output_path: str) -> None:
    """Write `output_path`.svg and, if a converter exists, `output_path`.png."""
    svg_path = output_path + '.svg'
    with open(svg_path, 'w') as f:
        f.write(svg_text)
    png_path = output_path + '.png'
    if os.path.exists(png_path):
        os.remove(png_path)
    for cmd in (['rsvg-convert', '-o', png_path, svg_path],
                ['convert', '-background', 'white', '-density', '160',
                 svg_path, png_path],
                ['inkscape', svg_path, '--export-type=png',
                 f'--export-filename={png_path}']):
        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
        # Some tools (e.g. snap inkscape) exit 0 without producing a file;
        # only accept a converter that actually wrote a non-empty PNG.
        if os.path.exists(png_path) and os.path.getsize(png_path) > 0:
            return
    # No converter available: the SVG was still written.


def _box_svg(cx, cy, hw, hh, gadget, name, init_state,
             loc_labels=None, state_labels=None, title=None, port_sides=None):
    """SVG fragment for one gadget box: shadow, body, internal transition arrows,
    and labelled perimeter ports.  Returns (svg_fragment, port_geometry)."""
    def fl(l):
        return loc_labels[l] if loc_labels and l in loc_labels else str(l)

    def fsname(s):
        return state_labels[s] if state_labels and s in state_labels else str(s)

    geo = _port_geometry(cx, cy, hw, hh, gadget.num_locations, port_sides)
    s = []
    r = 16
    # shadow + body
    s.append(f'<rect x="{cx-hw+3:.1f}" y="{cy-hh+3:.1f}" width="{2*hw:.1f}" '
             f'height="{2*hh:.1f}" rx="{r}" ry="{r}" fill="#00000022"/>')
    s.append(f'<rect x="{cx-hw:.1f}" y="{cy-hh:.1f}" width="{2*hw:.1f}" '
             f'height="{2*hh:.1f}" rx="{r}" ry="{r}" '
             f'fill="#e7dcf2" stroke="#2b2b2b" stroke-width="2.5"/>')
    if title:
        s.append(f'<text x="{cx:.1f}" y="{cy-hh-8:.1f}" text-anchor="middle" '
                 f'font-size="14" font-weight="bold" font-family="sans-serif">{title}</text>')
    s.append(f'<text x="{cx:.1f}" y="{cy-hh+15:.1f}" text-anchor="middle" '
             f'font-size="10" fill="#555" font-family="sans-serif">'
             f'{name} · init {fsname(init_state)}</text>')

    # internal transition arrows; transitions sharing a tunnel (same unordered
    # port pair, e.g. the two directions of a toggle) are bowed apart so both
    # are visible.
    trans = sorted(gadget.transitions)
    pair_total, pair_seen, pair_perp = {}, {}, {}
    for fs_, fl_, ts_, tl_ in trans:
        key = frozenset((fl_, tl_))
        pair_total[key] = pair_total.get(key, 0) + 1
    # A screen-consistent perpendicular per tunnel (from lower to higher index),
    # so both directions of a tunnel bow to opposite, stable sides.
    for key in pair_total:
        lo, hi = sorted(key)
        ax, ay = geo[lo][:2]
        bx, by = geo[hi][:2]
        length = math.hypot(bx - ax, by - ay) or 1.0
        pair_perp[key] = (-(by - ay) / length, (bx - ax) / length)
    for fs_, fl_, ts_, tl_ in trans:
        key = frozenset((fl_, tl_))
        total = pair_total[key]
        j = pair_seen.get(key, 0)
        pair_seen[key] = j + 1
        bow = 0.0 if total == 1 else (j - (total - 1) / 2) * 30.0
        a = geo[fl_][:2]
        b = geo[tl_][:2]
        # pull endpoints slightly inside so heads/tails sit just off the ports
        a = (a[0] + geo[fl_][2] * -10, a[1] + geo[fl_][3] * -10)
        b = (b[0] + geo[tl_][2] * -12, b[1] + geo[tl_][3] * -12)
        color, dash = _state_style(fs_)
        path, ctrl = _arrow_path(a, b, pair_perp[key], bow)
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ''
        s.append(f'<path d="{path}" fill="none" stroke="{color}" '
                 f'stroke-width="2"{dash_attr}/>')
        s.append(_arrowhead(b, b[0] - ctrl[0], b[1] - ctrl[1], color))
        # label on the actual curve (peak at t=0.5 is midpoint + perp*bow/2)
        mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        lx = mx + pair_perp[key][0] * bow * 0.5
        ly = my + pair_perp[key][1] * bow * 0.5
        s.append(_label(lx, ly, f'{fsname(fs_)}→{fsname(ts_)}', color))

    # ports + labels
    for i, (px, py, nx, ny) in geo.items():
        s.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="4.5" fill="#1a1a1a"/>')
        s.append(f'<text x="{px+nx*13:.1f}" y="{py+ny*13+4:.1f}" '
                 f'text-anchor="middle" font-size="11" font-weight="bold" '
                 f'font-family="sans-serif">{fl(i)}</text>')
    return '\n'.join(s), geo


def _svg_defs():
    # Arrowheads and label backgrounds are drawn explicitly (as polygons/rects)
    # for maximum rasteriser compatibility, so no marker defs are needed.
    return ''


def visualize_gadget_box(gadget: Gadget, output_path: str, format: str = 'png',
                         state_labels: Optional[Dict[int, str]] = None,
                         loc_labels: Optional[Dict[int, str]] = None,
                         title: str = '',
                         port_sides: Optional[Dict[int, str]] = None) -> None:
    """
    Draw a single gadget the paper way: a rounded box with its locations as
    labelled ports on the perimeter (cyclic order visible) and its transitions
    as arrows inside, one line style/colour per originating state.

    `port_sides` optionally pins each location to an edge ('L'/'R'/'T'/'B'),
    e.g. to lay tunnels out horizontally (loc 0 -> 'L', loc 1 -> 'R', ...).
    """
    hw, hh = 110, max(60, 30 * gadget.num_locations + 18)
    cx, cy = hw + 24, hh + 40
    frag, _ = _box_svg(cx, cy, hw, hh, gadget, name=title or 'gadget',
                       init_state=0, loc_labels=loc_labels,
                       state_labels=state_labels, title=title or None,
                       port_sides=port_sides)
    width, height = cx + hw + 20, cy + hh + 20
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
           f'height="{height}" viewBox="0 0 {width} {height}" '
           f'font-family="sans-serif">\n{_svg_defs()}\n{frag}\n</svg>')
    _svg_to_png(svg, output_path)


def visualize_system(system, output_path: str, format: str = 'png',
                     positions: Optional[Dict[str, Tuple[int, int]]] = None,
                     highlight_entry: Optional[str] = None,
                     highlight_moves: Optional[List[Tuple]] = None,
                     title: str = '',
                     state_labels: Optional[Dict[str, Dict[int, int]]] = None,
                     port_sides: Optional[Dict[str, Dict[int, str]]] = None) -> None:
    """
    Paper-style diagram of a GadgetSystem.

    Each instance is a rounded box with its ports on the perimeter (cyclic
    order visible) and its transitions drawn inside (one line style per state).
    Connected ports are joined by wires that leave each box perpendicular to its
    edge; exposed external ports get a green labelled tag.

    `positions` optionally places instances on a grid: `{name: (col, row)}`
    (default: a single row).  If `highlight_moves` (from GadgetSystem.trace) and
    `highlight_entry` are given, the traced path is overlaid in red with
    numbered steps.

    `state_labels` may map instance name -> {state index: label}.
    """
    names = system.instance_names
    if positions is None:
        positions = {n: (i, 0) for i, n in enumerate(names)}

    max_loc = max((system.types[n].num_locations for n in names), default=2)
    hw = 95
    hh = max(70, 26 * max_loc + 18)
    cell_w = 2 * hw + 170
    cell_h = 2 * hh + 110
    margin_x, margin_y = 120, 80

    centers = {}
    for n in names:
        col, row = positions[n]
        centers[n] = (margin_x + col * cell_w + hw,
                      margin_y + row * cell_h + hh)

    def sl(name):
        return state_labels.get(name) if state_labels else None

    def ps(name):
        return port_sides.get(name) if port_sides else None

    # boxes (collect port geometry)
    box_frags = []
    geo = {}  # (name, loc) -> (px,py,nx,ny)
    for n in names:
        cx, cy = centers[n]
        frag, g = _box_svg(cx, cy, hw, hh, system.types[n], n,
                           system.initial_state[n], state_labels=sl(n),
                           title=None, port_sides=ps(n))
        box_frags.append(frag)
        for loc, pg in g.items():
            geo[(n, loc)] = pg

    # wires
    wire_frags = []
    for a, b in system.connections:
        pa = geo[(a[0], a[1])]
        pb = geo[(b[0], b[1])]
        wire_frags.append(
            f'<path d="{_wire_path(pa[:2], pa[2:], pb[:2], pb[2:])}" '
            f'fill="none" stroke="#5f6368" stroke-width="2.4"/>')

    # external interface tags
    ext_frags = []
    ext_tag_pos = {}
    for label in system.external_labels:
        name, loc = system.external[label]
        px, py, nx, ny = geo[(name, loc)]
        tx, ty = px + nx * 60, py + ny * 60
        ext_tag_pos[label] = (tx, ty)
        ext_frags.append(
            f'<path d="M {px:.1f} {py:.1f} L {tx:.1f} {ty:.1f}" '
            f'stroke="#188038" stroke-width="2.4" fill="none"/>')
        ext_frags.append(
            f'<rect x="{tx-15:.1f}" y="{ty-13:.1f}" width="30" height="26" rx="5" '
            f'fill="white" stroke="#188038" stroke-width="2.2"/>')
        ext_frags.append(
            f'<text x="{tx:.1f}" y="{ty+5:.1f}" text-anchor="middle" '
            f'font-size="14" font-weight="bold" fill="#188038" '
            f'font-family="sans-serif">{label}</text>')

    # trace overlay
    overlay = []
    if highlight_moves is not None:
        for step, move in enumerate(highlight_moves, 1):
            if move[0] == 'traverse':
                _, name, fl_, tl_, _fs, _ts = move
                a = geo[(name, fl_)]
                b = geo[(name, tl_)]
                a2 = (a[0] + a[2] * -10, a[1] + a[3] * -10)
                b2 = (b[0] + b[2] * -12, b[1] + b[3] * -12)
                path, mid = _arrow_path(a2, b2, None, 0)
                overlay.append(f'<path d="{path}" fill="none" stroke="#d93025" '
                               f'stroke-width="3.2"/>')
                overlay.append(_arrowhead(b2, b2[0] - mid[0], b2[1] - mid[1],
                                          '#d93025', size=13))
                # offset the step badge off the arrow so it clears the
                # transition label sitting at the arrow's midpoint
                badge = (mid[0], mid[1] - 22)
            else:  # wire
                _, p, q = move
                pa = geo[(p[0], p[1])]
                pb = geo[(q[0], q[1])]
                overlay.append(
                    f'<path d="{_wire_path(pa[:2], pa[2:], pb[:2], pb[2:])}" '
                    f'fill="none" stroke="#d93025" stroke-width="3.2"/>')
                badge = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
            overlay.append(
                f'<circle cx="{badge[0]:.1f}" cy="{badge[1]:.1f}" r="9" '
                f'fill="white" stroke="#d93025" stroke-width="2"/>')
            overlay.append(
                f'<text x="{badge[0]:.1f}" y="{badge[1]+4:.1f}" text-anchor="middle" '
                f'font-size="11" font-weight="bold" fill="#d93025" '
                f'font-family="sans-serif">{step}</text>')
        if highlight_entry is not None and highlight_entry in ext_tag_pos:
            tx, ty = ext_tag_pos[highlight_entry]
            overlay.append(
                f'<text x="{tx:.1f}" y="{ty-20:.1f}" text-anchor="middle" '
                f'font-size="11" fill="#d93025" font-family="sans-serif">enter</text>')

    # canvas size
    max_col = max(c for c, _ in positions.values())
    max_row = max(r for _, r in positions.values())
    width = margin_x + max_col * cell_w + 2 * hw + 130
    height = margin_y + max_row * cell_h + 2 * hh + 90
    head = ''
    if title:
        head = (f'<text x="{width/2:.1f}" y="30" text-anchor="middle" '
                f'font-size="16" font-weight="bold" font-family="sans-serif">{title}</text>')

    body = '\n'.join(wire_frags + ext_frags + box_frags + overlay)
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
           f'height="{height}" viewBox="0 0 {width} {height}" '
           f'font-family="sans-serif">\n<rect width="{width}" height="{height}" '
           f'fill="white"/>\n{_svg_defs()}\n{head}\n{body}\n</svg>')
    _svg_to_png(svg, output_path)
