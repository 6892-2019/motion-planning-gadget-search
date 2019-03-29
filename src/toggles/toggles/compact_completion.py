import argparse
from sqlalchemy.sql import text

from .models import session_scope


find_mergeable_query = """
select p, q from (
  select id as p, r as a,
    lead(id) over (order by lower(r)) as q,
    lead(r) over (order by lower(r)) as b
  from {}
) as x where q is not null and (
  upper(a) = lower(b) or
  not exists (select 1 from gadgets where id >= upper(a) and id < lower(b))
  {}
) order by lower(a)
"""

extreme_values_query = """
select v.q, lower(a.r), upper(b.r) from
  (values {values}) as v(p, q)
  join {table} as a on v.p = a.id
  join {table} as b on v.q = b.id
"""

def build_extreme_values_query(groups, table):
    things = []
    for k, g in groups.items():
        assert k == g[-1]
        things.append('({}, {})'.format(g[0], g[-1]))
    return extreme_values_query.format(values=', '.join(things), table=table)

delete_query = """
delete from {} where id in ({})
"""

def build_delete_rows_query(groups, table):
    things = []
    for _, g in groups.items():
        things.extend(map(str, g))
    return delete_query.format(table, ', '.join(things))

insert_query = """
insert into {} (r) values {}
"""

def build_insert_rows_query(replacements, table):
    things = []
    for _, g in replacements.items():
        things.append('(int8range({}, {}))'.format(g[0], g[1]))
    return insert_query.format(table, ', '.join(things))


def compact_completion(args):
    with session_scope() as conn:
        pairs = []
        for r in conn.execute(text(find_mergeable_query.format('completed_mirrors', ''))):
            pairs.append((int(r[0]), int(r[1])))
        if not pairs:
            print('{}: nothing to compact'.format('completed_mirrors'))
            return

        # If the p[0] is the last element of some group, make p[1] the new last
        # element of that group.  Otherwise, start a new group with just those elements.
        groups = {}
        for p, q in pairs:
            if p in groups:
                groups[p].append(q)
                groups[q] = groups.pop(p)
            else:
                groups[q] = [p, q]

        # We retain the group we're replacing, but don't use it for anything.
        # If we wanted to replace in batches instead of two huge queries, we
        # could try to replace just after deleting a group, I guess.  (It's all
        # in a transaction, so doesn't actually matter.)
        replacements = {}
        for r in conn.execute(text(build_extreme_values_query(groups, 'completed_mirrors'))):
            replacements[int(r[0])] = (int(r[1]), int(r[2]))

        conn.execute(text(build_delete_rows_query(groups, 'completed_mirrors')))
        conn.execute(text(build_insert_rows_query(replacements, 'completed_mirrors')))

    deleted = sum([len(g) for g in groups.values()])
    print('{}: deleted {} rows in {} groups, inserted {}, net {}',
        'completed_mirrors', deleted, len(groups), len(replacements), len(replacements) - deleted)


def register_subcommand(parser: argparse.ArgumentParser, subparser_holder: argparse._SubParsersAction):
    cc_parser = subparser_holder.add_parser('compact-completion')
    cc_parser.set_defaults(command_func=compact_completion)
