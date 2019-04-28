import sys
import itertools
import argparse
from operator import itemgetter

from sqlalchemy import or_, cast, and_, func
from sqlalchemy.dialects.postgresql import array, INT8RANGE, ARRAY
from sqlalchemy.dialects.postgresql.array import CONTAINED_BY
from psycopg2.extras import NumericRange

from toggles.runner import local_toggles_runner
from .models import session_scope, Name, Gadget, CombineEdge
from .util import parse_gid_spec

# What do we do when we've combined some but not all of the lefts with the rights?
# We could split the rights up but then we're sending the lefts a bunch of times.

# We should have a --singleplayer/--multiplayer option.  In singleplayer mode we
# only combine gadgets that have been closed (because closure is safe in
# singleplayer), while in multiplayer we have to do everything.

# combine is the only binary relation, hence it gets its own file.


def combine(args):
    left_gids, left_gid_ranges, left_names = parse_gid_spec(args.left)
    right_gids, right_gid_ranges, right_names = parse_gid_spec(args.right)

    # Fetch all the right ids, then fetch all the left ids that haven't been
    # combined with any of the right ids or were combined with lesser precision.
    # Then fetch the gadget data for all those ids.
    with session_scope() as session:
        # Resolve names.
        names = session.query(Name).filter(or_(Name.name.in_(left_names), Name.name.in_(right_names))).all()
        name_to_gids = {}
        for n in names:
            name_to_gids.setdefault(n.name, []).append(n.gadget_id)
        for n in left_names:
            left_gids.update(name_to_gids[n])
        for n in right_names:
            right_gids.update(name_to_gids[n])

        right_gid_ranges = [NumericRange(lower=a, upper=b) for a, b in right_gid_ranges]
        # Yes, we really have to cast.  It's considered text otherwise.
        range_array = cast(array(right_gid_ranges, type_=INT8RANGE), ARRAY(INT8RANGE))
        right_gids = set(map(itemgetter(0), session.query(Gadget.id).filter(
            and_(
                or_(
                    Gadget.id.in_(right_gids),
                    range_array.any(Gadget.id, operator=CONTAINED_BY)
                ),
                Gadget.locations < args.precision
                # TODO: single-player check would go here
            )
        ).all()))

        left_gid_ranges =  [NumericRange(lower=a, upper=b) for a, b in left_gid_ranges]
        range_array = cast(array(left_gid_ranges, type_=INT8RANGE), ARRAY(INT8RANGE))
        left_gids = set(map(itemgetter(0), session.query(Gadget.id).filter(
            and_(
                or_(
                    Gadget.id.in_(left_gids),
                    range_array.any(Gadget.id, operator=CONTAINED_BY)
                ),
                Gadget.locations < args.precision,  # TODO: args.precision - max(right.locations)
                # We only want those ids where at least one combine is missing.
                session.query(func.count(CombineEdge.input2)).filter(
                    and_(
                        CombineEdge.input1 == Gadget.id,
                        CombineEdge.input2.in_(right_gids)  # this could be an or_ to use ranges
                    )
                ).as_scalar() < len(right_gids)
                # TODO: single-player check would go here
            )
        ).all()))

        combine_data = session.query(Gadget.id, Gadget.data).filter(
            or_(
                Gadget.id.in_(left_gids),
                Gadget.id.in_(right_gids)
            )
        ).all()

    if not combine_data:
        print('ERROR: no pending combines for those inputs')
        sys.exit(1)

    rows, edges, toughie_id_pairs = local_toggles_runner(args, 'combine',
            # TODO: teach msgpack to serialize a set (sigh)
            [combine_data, list(left_gids), list(right_gids), args.precision])

    # TODO: mostly copied from connect.py
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

        for e in edges:
            e[2] = local_to_global[e[2]]
            session.add(CombineEdge.from_tuple(e))


def register_subcommand(parser: argparse.ArgumentParser, subparser_holder: argparse._SubParsersAction):
    connect_parser = subparser_holder.add_parser('combine')
    connect_parser.add_argument('--precision', type=int, required=True)
    connect_parser.add_argument('--left', type=str, nargs='+', required=True)
    connect_parser.add_argument('--right', type=str, nargs='+', required=True)
    connect_parser.set_defaults(command_func=combine)
