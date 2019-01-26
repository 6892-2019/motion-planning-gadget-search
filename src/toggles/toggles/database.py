#!/usr/bin/env python3

import argparse
import os
import sys
import re
import collections
import pwd
import subprocess
import msgpack
import yaml
import sqlalchemy
from sqlalchemy import bindparam, select
from sqlalchemy.dialects.postgresql import insert



class SingleDefinitionMapping(collections.abc.MutableMapping):
  def __init__(self, *args, **kwargs):
    self._dict = dict(*args, **kwargs)
  def __getitem__(self, key):
    return self._dict[key]
  def __setitem__(self, key, value):
    if key in self:
      raise ValueError('{} {} {}'.format(key, self[key], value))
    self._dict[key] = value
  def __delitem__(self, key):
    del self._dict[key]
  def __iter__(self):
    return iter(self._dict)
  def __len__(self):
    return len(self._dict)



def make_msgpack_rpc_call(command, *args, sequence_number=0, compress=False):
  if compress:
    raise NotImplementedError('RPC call compression not implemented')
  if not isinstance(command, str):
    raise ValueError('command {} is not a str'.format(command))
  return msgpack.packb([0, sequence_number, command, args], use_bin_type=True)

def unpack_msgpack_rpc_response(resp_bytes, expected_sequence_number=0, compress=False):
  if compress:
    raise NotImplementedError('RPC response compression not implemented')
  resp = msgpack.unpackb(resp_bytes, raw=False)
  if len(resp) != 4 or resp[0] != 1:
    raise ValueError('malformed RPC response: {}'.format(resp))
  if resp[1] != expected_sequence_number:
    raise ValueError('unexpected sequence number: expected {}, got {}'.format(expected_sequence_number, resp[1]))
  return (resp[2], resp[3])

def local_toggles_runner(script_args, command, *args, error_as_exc=True):
  # We're not using the sequence number for anything here.
  cmd_bytes = make_msgpack_rpc_call(command, *args)
  with open('/tmp/errorcmd.bin', 'wb') as whatever:
    whatever.write(cmd_bytes)
  
  results = subprocess.run([script_args.runner_path], check=True, input=cmd_bytes, capture_output=True)
  error, retval = unpack_msgpack_rpc_response(results.stdout)
  if error:
    raise ValueError('RPC returned error: command {}, args {}, error {}'.format(command, args, error))
  return retval

def discover_database(args):
  connect_url = 'postgresql+psycopg2://{db_user}:{db_pass}@{db_host}:{db_port}/{db_name}'.format(**vars(args))
  engine = sqlalchemy.create_engine(connect_url, isolation_level='SERIALIZABLE')
  meta = sqlalchemy.MetaData()
  meta.reflect(engine)
  return (engine, meta)



def sync_known_gadgets(args):
  input_data = yaml.load(args.input)
  db_engine, db_meta = discover_database(args)

  named_rows = SingleDefinitionMapping()
  alias_groups = SingleDefinitionMapping()
  mirror_provenance = [] # list of pairs
  for name, gadget in input_data['gadgets'].items():
    for required_key in ('uedges', 'dedges'):
      if required_key not in gadget:
        gadget[required_key] = []
    canonicals = local_toggles_runner(args, 'canonicalize', gadget)
    assert 1 <= len(canonicals) <= 2
    if len(canonicals) == 1:
      named_rows[name] = canonicals[0]
    else:
      named_rows['r-'+name] = canonicals[0]
      named_rows['s-'+name] = canonicals[1]
      alias_groups[name] = ('r-'+name, 's-'+name)
      mirror_provenance.append(('r-'+name, 's-'+name))

  for alias, target in input_data['aliases'].items():
    if alias in named_rows:
      print('ERROR: alias {} (intended for {}) already names gadget in input file'.format(alias, target))
      sys.exit(1)
    if alias in alias_groups:
      print('ERROR: alias {} (intended for {}) already names an alias group (standing for {})'.format(alias, target, alias_groups[alias]))
      sys.exit(1)
    # This means aliases can't target other aliases, but I think that's fine.
    if target not in named_rows and target not in alias_groups:
      print('ERROR: alias {} with target {} names neither a gadget nor an alias group')
      sys.exit(1)
    if target in alias_groups:
      alias_groups[alias] = alias_groups[target]
    else:
      alias_groups[alias] = (target,)

  name_to_gid = {}
  with db_engine.connect() as conn, conn.begin() as txn:
    gadgets_tbl = db_meta.tables['gadgets']
    q_existing_gadget_id = select([gadgets_tbl.c.id]).where(gadgets_tbl.c.edges == bindparam('data'))
    q_insert_gadget = gadgets_tbl.insert().returning(gadgets_tbl.c.id)
    gadget_non_primary_keys = [c.name for c in gadgets_tbl.c if not gadgets_tbl.primary_key.contains_column(c)]

    for name, row in named_rows.items():
      gid = conn.execute(q_existing_gadget_id, data=row[4]).scalar()
      if not gid:
        gid = conn.execute(q_insert_gadget, {k: v for k, v in zip(gadget_non_primary_keys, row)}).scalar()
      name_to_gid[name] = gid

    for aname, bname in mirror_provenance:
      a, b = name_to_gid[aname], name_to_gid[bname]
      a, b = min(a, b), max(a, b)
      conn.execute(sqlalchemy.text('insert into mirror_provenance values ({}, {}) on conflict do nothing'.format(a, b)))
      q_insert_compmirr = 'insert into completed_mirrors (r) values (int8range({}, {})) on conflict do nothing'
      conn.execute(sqlalchemy.text(q_insert_compmirr.format(a, a+1)))
      conn.execute(sqlalchemy.text(q_insert_compmirr.format(b, b+1)))

    # Previously we tried to upsert aliases and cnames, but it's too much
    # complexity for too little benefit.
    conn.execute('truncate table aliases, cnames restart identity')
    aliases_tbl, cnames_tbl = db_meta.tables['aliases'], db_meta.tables['cnames']
    all_aliases = [{'gid': v, 'name': k} for k, v in name_to_gid.items()]
    q_insert_cname = insert(cnames_tbl).values(gadget_id=bindparam('gid'), name=bindparam('name'))
    conn.execute(q_insert_cname, all_aliases)

    for name, targets in alias_groups.items():
      all_aliases.extend([{'gid': name_to_gid[t], 'name': name} for t in targets])
    q_insert_alias = insert(aliases_tbl).values(gadget_id=bindparam('gid'), name=bindparam('name'))
    conn.execute(q_insert_alias, all_aliases)



def connect(args):
  # Three kinds of queries: fetch ids, fetch all ids in range, and fetch with a join through the aliases table.  An empty range is okay (but warnable?), an invalid id or alias is not.  We could do faster batch queries if we didn't care about checking for errors.  We should also be deduplicating and giving useful error messages.
  gids = set()
  gid_ranges = [] # pairs in [) form
  g_names = []
  for i in args.inputs:
    m = re.fullmatch('\d+', i)
    if m:
      if int(i) in gids: # set add returns None, grumble
        print('warning: skipping duplicate id', i)
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
        raise ValueError('bad range: '+i)
      gid_ranges.append((lower, upper))
      continue

    g_names.append(i) #no useful consistency check on aliases, though it might be a mistyped range...

  db_engine, db_meta = discover_database(args)
  gadgets_tbl = db_meta.tables['gadgets']
  aliases_tbl = db_meta.tables['aliases']
  with db_engine.connect() as conn:
    with conn.begin() as txn:
      conn.execute('set transaction read only') # begin() doesn't take an arg

      gid_to_alias = {}
      q_translate_alias = select([aliases_tbl.c.gadget_id]).where(aliases_tbl.c.name == bindparam('name'))
      for name in g_names:
        rows = conn.execute(q_translate_alias, name=name).fetchall()
        if not rows:
          raise ValueError('no match for {}'.format(name))
        for row in rows:
          i = row[0]
          if i in gids:
            print('warning: named gadget {} duplicates explicit id {}'.format(name, i))
          for r in gid_ranges:
            if r[0] <= i and i < r[1]:
              print('warning: named gadget {} ({}) contained in range [{},{})'.format(name, i, r[0], r[1]))
          if i in gid_to_alias:
            print('warning: named gadget {} duplicates other named gadget {} (both {})'.format(name, gid_to_alias[i], i))
          gid_to_alias[i] = name # only tracking the last duplicate is fine
      gids.update(gid_to_alias.keys())

      clauses = []
      if gids:
        clauses.append(gadgets_tbl.c.id.in_(gids))
      if gid_ranges:
        data = ', '.join(['int8range({}, {})'.format(lower, upper) for lower, upper in gid_ranges])
        clauses.append(sqlalchemy.text('gadgets.id <@ any(array['+data+'])'))
      if not clauses:
        raise ValueError("can't happen: neither ids nor ranges?")
      q_retrieve_gadgets = select([gadgets_tbl.c.id, gadgets_tbl.c.edges]).where(sqlalchemy.or_(*clauses)).where(sqlalchemy.text("NOT EXISTS (SELECT 1 FROM completed_connects WHERE completed_connects.r @> gadgets.id LIMIT 1)"))
      gadgets = conn.execute(q_retrieve_gadgets).fetchall()

    # exit the read-only transaction here



if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  subparsers = parser.add_subparsers()

  parser.add_argument('--runner-path', type=str, help='Path to local toggles-runner executable', default='build/debug/bin/toggles-runner.exe')

  dbopts = parser.add_argument_group('Database options')
  dbopts.add_argument('--db-user', '--db-username', type=str, default=pwd.getpwuid(os.getuid()).pw_name)
  dbopts.add_argument('--db-pass', '--db-password', type=str, default='')
  dbopts.add_argument('--db-host', '--db-hostname', type=str, default='127.0.0.1')
  dbopts.add_argument('--db-port', type=int, default=5432)
  dbopts.add_argument('--db-name', type=str, default='togglesearch')

  skg_parser = subparsers.add_parser('sync-known-gadgets')
  skg_parser.add_argument('input', type=argparse.FileType(), help='YAML file of gadget and alias definitions')
  skg_parser.set_defaults(command_func=sync_known_gadgets)

  connect_parser = subparsers.add_parser('connect')
  connect_parser.add_argument('inputs', type=str, nargs='+', help='Name, gadget id, or range of gadget ids to connect (may be passed multiple times)')
  connect_parser.set_defaults(command_func=connect)

  args = parser.parse_args()
  args.command_func(args)

# connect to database (all subcommands)
# sync-known-gadgets subcommand:
#  read yaml
#  canonicalize each gadget (using local toggles-runner process)
#  learn id of existing gadgets (shared batch operation)
#  insert nonexisting gadgets (shared batch operation)
#  learn id of existing cnames (shared batch operation)
#  update any incorrect cnames
#  insert nonexisting cnames
#  learn id of existing aliases (shared batch operation)
#  update any incorrect aliases
#  insert nonexisting aliases
#  optionally delete any cnames/aliases not specified in the YAML file
#  warn about missing cnames

# add-gadget
# set-cname <name> <gadget-id>
#   if existing cname for that gadget, optionally demote it to an alias
#   if cname points to different gadget
