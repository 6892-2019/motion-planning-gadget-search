import argparse
import sys
from operator import itemgetter
from more_itertools import consecutive_groups

from sqlalchemy import and_, or_, cast
from sqlalchemy.dialects.postgresql import array, INT8RANGE, ARRAY
from sqlalchemy.dialects.postgresql.array import CONTAINED_BY
from psycopg2.extras import NumericRange

from .models import session_scope, Name, Gadget, ConnectEdge, CompletedConnect, MirrorEdge, CompletedMirror, CloseEdge, CompletedClose
from .runner import local_toggles_runner
from .util import parse_gid_spec


def unary(args, command_str, edge_cls, completed_cls):
    gids, gid_ranges, input_names = parse_gid_spec(args.inputs)

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
                ~session.query(completed_cls).filter(completed_cls.r.contains(Gadget.id)).exists()
            )
        ).all()

    if not connect_data:
        print('ERROR: no pending {}s for those inputs'.format(command_str))
        sys.exit(1)

    rows, edges = local_toggles_runner(args, command_str, connect_data)

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
        novel_gadget_ids = [p[1].id for p in pending]

        completed_ids = map(itemgetter(0), connect_data)
        if not edge_cls.directed:
            # For unidirectional edges, we also completed for any new gadgets
            # we found, so make it a set.  We start from the specified ids
            # because we tried them even if we didn't find any edges from them.
            completed_ids = set(completed_ids)
        for e in edges:
            e[1] = local_to_global[e[1]]
            if not edge_cls.directed:
                # We need to sort our edges.
                if e[0] > e[1]:
                    e[0], e[1] = e[1], e[0]
                # Both ends are completed.
                completed_ids.add(e[0])
                completed_ids.add(e[1])

        if not edge_cls.directed:
            # We have to deduplicate our edges because we might have found both
            # directions.  (If we switch to bulk inserts this can be ON CONFLICT
            # DO NOTHING.)
            edges = set(map(tuple, edges))

        # TODO: both edges and completion logging could be done in bulk
        session.add_all(map(edge_cls.from_tuple, edges))

        # There would only be conflicts here if some other task did the work
        # first, but in that case we'd have conflicted on the edges too, so we
        # can use maximal intervals.
        for group in consecutive_groups(sorted(completed_ids)):
            group = tuple(group)  # force
            session.add(completed_cls.range(group[0], group[-1]+1))

    if args.print_novel:
        if novel_gadget_ids:
            print(len(novel_gadget_ids), 'novel gadgets:', ' '.join(map(str, novel_gadget_ids)))
        else:
            print('No novel gadgets discovered.')


def connect(args):
    return unary(args, 'connect', ConnectEdge, CompletedConnect)


def close(args):
    return unary(args, 'close', CloseEdge, CompletedClose)


def mirror(args):
    return unary(args, 'mirror', MirrorEdge, CompletedMirror)


def register_subcommand(parser: argparse.ArgumentParser, subparser_holder: argparse._SubParsersAction):
    connect_parser = subparser_holder.add_parser('connect')
    connect_parser.add_argument('inputs', type=str, nargs='+')
    connect_parser.add_argument('--print-novel', action='store_true')
    connect_parser.set_defaults(command_func=connect)

    close_parser = subparser_holder.add_parser('close')
    close_parser.add_argument('inputs', type=str, nargs='+')
    close_parser.add_argument('--print-novel', action='store_true')
    close_parser.set_defaults(command_func=close)

    mirror_parser = subparser_holder.add_parser('mirror')
    mirror_parser.add_argument('inputs', type=str, nargs='+')
    mirror_parser.add_argument('--print-novel', action='store_true')
    mirror_parser.set_defaults(command_func=mirror)
