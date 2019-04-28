import re
from typing import Sequence, List, Tuple, Set


def parse_gid_spec(inputs: Sequence[str]) -> (Set[int], List[Tuple[int, int]], List[str]):
    gids = set()
    gid_ranges = []  # pairs in [) form
    input_names = []
    for i in inputs:
        m = re.fullmatch('\d+', i)
        if m:
            gids.add(int(i))
            continue

        m = re.fullmatch(r'(\(|\[)(\d+),\s*(\d+)(\)|\])', i)
        if m:
            lower, upper = int(m[2]), int(m[3])
            if m[1] == '(':
                lower += 1
            if m[4] == ']':
                upper += 1
            if not (lower < upper):
                raise ValueError('bad range: ' + i)
            gid_ranges.append((lower, upper))
            continue

        input_names.append(i)
    return gids, gid_ranges, input_names
