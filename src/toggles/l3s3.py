#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2019 Massachusetts Institute of Technology

# Parses mxGraph/draw.io XML into gadget definitions.  Expects grouped grids of
# dots connected by lines.  All lines must connect to dots; in theory we could
# interpret sourcePoint and targetPoint geometry (expecting it to substantially
# overlap with a dot), but we don't currently.

import sys, operator
import xml.etree.ElementTree as ET
import yaml
try:
    from yaml import CLoader as Loader, CSafeDumper as Dumper
except ImportError:
    from yaml import SafeLoader as Loader, SafeDumper as Dumper

tree = ET.parse(sys.stdin)
overall = []
for group in tree.findall(".//mxCell[@style='group']"):
    # draw.io gets snapping wrong sometimes, so we round for tolerance.
    base_x, base_y = round(float(group[0].attrib['x'])), round(float(group[0].attrib['y']))
    members = tree.findall(".//mxCell[@parent='{}']".format(group.attrib['id']))
    points = [m for m in members if 'ellipse' in m.attrib['style']]
    edges = [m for m in members if 'ellipse' not in m.attrib['style']]
    def point_sort(p):
        x, y = base_x, base_y
        if 'x' in p[0].attrib:
            x += int(p[0].attrib['x'])
        if 'y' in p[0].attrib:
            y += int(p[0].attrib['y'])
        # Round down to a multiple of 10, so that rows are reasonably well-aligned.
        # This worked on l3s3 because they were mostly at multiples of 5 (but
        # sometimes ended in 6).  We should really be treating them as intervals
        # that should overlap, but that's complicated.
        x, y = x - x % 10, y - y % 10
        return y, x # row-major order
    points.sort(key=point_sort)
    vertex_id_to_vertex = {p.attrib['id']: (points.index(p) // 3, points.index(p) % 3) for p in points}
    uedges = []
    for e in edges:
        s, t = vertex_id_to_vertex[e.attrib['source']], vertex_id_to_vertex[e.attrib['target']]
        s, t = (t, s) if s > t else (s, t)
        uedges.append((s[0], s[1], t[1], t[0])) # SLLS format
    uedges.sort()
    overall.append((point_sort(points[0]), uedges))

overall.sort(key=operator.itemgetter(0))
overall = [o[1] for o in overall]

# https://stackoverflow.com/questions/28974357/specifying-styles-for-portions-of-a-pyyaml-dump-ii-sequences?noredirect=1&lq=1
class flowseq(list): pass
def flowseq_rep(dumper, data):
    return dumper.represent_sequence(u'tag:yaml.org,2002:seq', data, flow_style=True)
yaml.add_representer(flowseq, flowseq_rep, Dumper=Dumper)

gadgets = {}
for i, uedges in enumerate(overall):
    gadgets['l3s3_{}'.format(i)] = {'uedges': flowseq(uedges)}
doc = {'gadgets': gadgets}
print(yaml.dump(doc, default_flow_style=None, Dumper=Dumper, sort_keys=False))