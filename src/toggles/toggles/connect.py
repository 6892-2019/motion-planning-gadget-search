import argparse
import re
import sys
from operator import itemgetter
from more_itertools import consecutive_groups

from sqlalchemy import and_, or_, cast
from sqlalchemy.dialects.postgresql import array, INT8RANGE, ARRAY
from sqlalchemy.dialects.postgresql.array import CONTAINED_BY
from psycopg2.extras import NumericRange

from .models import session_scope, Name, Gadget, CompletedConnect, ConnectEdge
from .runner import local_toggles_runner


def connect(args):
    gids = set()
    gid_ranges = []  # pairs in [) form
    input_names = []
    for i in args.inputs:
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

    # TODO: we'd like a read-only session if possible here, we're not going to
    # hold it open while the compute happens
    with session_scope() as session:
        # Resolve names.
        names = session.query(Name).filter(Name.name.in_(input_names)).all()
        name_to_gids = {}
        for n in names:
            name_to_gids.setdefault(n.name, []).append(n.gadget_id)
        for n in input_names:
            gids.update(name_to_gids[n])

        gid_ranges = [NumericRange(lower=a, upper=b) for a, b in gid_ranges]
        # Yes, we really have to cast.  It's considered text otherwise.
        range_array = cast(array(gid_ranges, type_=INT8RANGE), ARRAY(INT8RANGE))
        connect_data = session.query(Gadget.id, Gadget.data).filter(
            and_(
                or_(
                    Gadget.id.in_(gids),
                    range_array.any(Gadget.id, operator=CONTAINED_BY)
                ),
                ~session.query(CompletedConnect).filter(CompletedConnect.r.contains(Gadget.id)).exists()
            )
        ).all()

    if not connect_data:
        print('ERROR: no pending connects for those inputs')
        sys.exit(1)

    rows, edges, toughie_ids = local_toggles_runner(args, 'connect', connect_data)

    with session_scope() as session:
        # Particularly for large batches, it may be faster to use a temporary
        # table, then insert the result of a subquery, then finally use a join
        # to get the id mapping.
        # https://stackoverflow.com/a/4070385/3614835
        local_to_global = {}
        pending = []
        for index, r in enumerate(rows):
            g = session.query(Gadget).filter_by(data=r[-1]).one_or_none()
            if g:
                local_to_global[index] = g.id
            else:
                g = Gadget.from_tuple(r)
                session.add(g)
                # We don't get an id until we flush, and we want to batch the flush.
                pending.append((index, g))
        session.flush()
        local_to_global.update({index: g.id for index, g in pending})

        # TODO: both edges and completion logging could be done in bulk
        for e in edges:
            assert e[1] < len(rows), e
            e[1] = local_to_global[e[1]]
            session.add(ConnectEdge.from_tuple(e))

        # There would only be conflicts here if some other task did the work
        # first, but in that case we'd have conflicted on the edges too, so we
        # can use maximal intervals.
        completed_ids = sorted(map(itemgetter(0), connect_data))
        for group in consecutive_groups(completed_ids):
            group = tuple(group)  # force
            session.add(CompletedConnect.range(group[0], group[-1]+1))


def register_subcommand(parser: argparse.ArgumentParser, subparser_holder: argparse._SubParsersAction):
    connect_parser = subparser_holder.add_parser('connect')
    connect_parser.add_argument('inputs', type=str, nargs='+', help='Name, gadget id, or range of gadget ids to connect (may be passed multiple times)')
    connect_parser.set_defaults(command_func=connect)
