#!/usr/bin/env python3
"""
Jupyter notebook helpers for inline gadget visualization.

Import this module in a notebook to get show() and show_comparison(), which
display gadget diagrams directly in the output cell without leaving temp files.

Requires graphviz and IPython (both present in any standard Jupyter install).
"""

import os
import tempfile
from typing import Dict, Optional

from gadget import Gadget
from gadget_viz import (visualize_gadget_circular, visualize_gadget_comparison,
                        visualize_gadget_automaton)
from gadget_simulation import print_traversal_table


def show(gadget: Gadget,
         title: str = '',
         state_labels: Optional[Dict[int, str]] = None,
         loc_labels: Optional[Dict[int, str]] = None) -> None:
    """
    Display a gadget inline in a Jupyter notebook (circular layout).

    Args:
        gadget: The gadget to display.
        title: Optional bold heading printed above the diagram.
        state_labels: Display names for states, e.g. {0: 'open', 1: 'closed'}.
        loc_labels: Display names for locations, e.g. {0: 'in', 1: 'out'}.
    """
    from IPython.display import display, Image, HTML
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as f:
        path = f.name
    try:
        visualize_gadget_circular(gadget, path, format='png',
                                  state_labels=state_labels,
                                  loc_labels=loc_labels)
        if title:
            display(HTML(f'<b>{title}</b>'))
        display(Image(path + '.png'))
    finally:
        for p in [path, path + '.png']:
            try:
                os.unlink(p)
            except FileNotFoundError:
                pass


def show_table(gadget: Gadget,
               take_closure: bool = False,
               state_labels: Optional[Dict[int, str]] = None,
               loc_labels: Optional[Dict[int, str]] = None,
               title: str = '') -> None:
    """Print the traversal-mode table inline (useful as a quick hand-check)."""
    print_traversal_table(gadget, take_closure=take_closure,
                          state_labels=state_labels,
                          loc_labels=loc_labels, title=title)


def show_automaton(gadget: Gadget,
                   title: str = '',
                   state_labels: Optional[Dict[int, str]] = None,
                   loc_labels: Optional[Dict[int, str]] = None) -> None:
    """
    Display a gadget as a state-machine (automaton) diagram inline in a Jupyter
    notebook.  States are nodes; edges are labelled with entry→exit port pairs.
    """
    from IPython.display import display, Image, HTML
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as f:
        path = f.name
    try:
        visualize_gadget_automaton(gadget, path, format='png',
                                   state_labels=state_labels,
                                   loc_labels=loc_labels, title=title)
        if title:
            display(HTML(f'<b>{title}</b>'))
        display(Image(path + '.png'))
    finally:
        for p in [path, path + '.png']:
            try:
                os.unlink(p)
            except FileNotFoundError:
                pass


def show_comparison(g1: Gadget, g2: Gadget,
                    title: str = '',
                    state_labels: Optional[Dict[int, str]] = None,
                    loc_labels: Optional[Dict[int, str]] = None) -> None:
    """
    Display two gadgets side by side inline in a Jupyter notebook.

    Useful for before/after closure comparisons or equivalence illustrations.

    Args:
        g1: Left gadget.
        g2: Right gadget.
        title: Optional bold heading printed above the diagram.
        state_labels: Display names for states (applied to both gadgets).
        loc_labels: Display names for locations (applied to both gadgets).
    """
    from IPython.display import display, Image, HTML
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as f:
        path = f.name
    try:
        visualize_gadget_comparison(g1, g2, path, format='png',
                                    state_labels=state_labels,
                                    loc_labels=loc_labels)
        if title:
            display(HTML(f'<b>{title}</b>'))
        display(Image(path + '.png'))
    finally:
        for p in [path, path + '.png']:
            try:
                os.unlink(p)
            except FileNotFoundError:
                pass
