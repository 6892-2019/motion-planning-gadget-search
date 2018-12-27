#!/usr/bin/env python3

import argparse
import os
import sys
import pwd
import subprocess
import msgpack
import yaml
import sqlalchemy
from sqlalchemy import bindparam
from sqlalchemy.dialects.postgresql import insert



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
  engine = sqlalchemy.create_engine(connect_url)
  meta = sqlalchemy.MetaData()
  meta.reflect(engine)
  return (engine, meta)



def sync_known_gadgets(args):
  input_data = yaml.load(args.input)
  db_engine, db_meta = discover_database(args)

  named_rows = {}
  for name, gadget in input_data['gadgets'].items():
    for required_key in ('uedges', 'dedges'):
      if required_key not in gadget:
        gadget[required_key] = []
    named_rows[name] = local_toggles_runner(args, 'canonicalize', gadget)

  name_to_gid = {}
  with db_engine.connect() as conn, conn.begin() as txn:
    gadgets_tbl = db_meta.tables['gadgets']
    q_existing_gadget_id = sqlalchemy.select([gadgets_tbl.c.id]).where(gadgets_tbl.c.edges == bindparam('data'))
    gadget_non_primary_keys = [c.name for c in gadgets_tbl.c if not gadgets_tbl.primary_key.contains_column(c)]

    for name, row in named_rows.items():
      gid = conn.execute(q_existing_gadget_id, data=row[4]).scalar()
      if not gid:
        gid = conn.execute(gadgets_tbl.insert().returning(gadgets_tbl.c.id), {k: v for k, v in zip(gadget_non_primary_keys, row)}).scalar()
      name_to_gid[name] = gid
    print(name_to_gid)

    for alias, cname in input_data['aliases'].items():
      if alias in name_to_gid:
        print('ERROR: alias {} (intended for {}) already names gadget in input file'.format(alias, cname))
        sys.exit(1)
      name_to_gid[alias] = name_to_gid[cname]

    aliases_tbl = db_meta.tables['aliases']
    q_upsert_alias = insert(aliases_tbl).values(gadget_id=bindparam('gid'), name=bindparam('name')).on_conflict_do_update(index_elements=(aliases_tbl.c.name,), set_={'gadget_id': bindparam('gid')}).returning(aliases_tbl.c.id)
    name_to_aliasid = {}
    for name, gid in name_to_gid.items():
      name_to_aliasid[name] = conn.execute(q_upsert_alias, gid=gid, name=name).scalar()

    # Ideally we'd upsert cnames, but there are two possible unique indices
    # that might be violated, possibly simultaneously, so it's not clear how to
    # resolve conflicts.
    conn.execute('truncate table cnames restart identity')
    cnames_tbl = db_meta.tables['cnames']
    q_insert_cnames = insert(cnames_tbl).values(gadget_id=bindparam('gid'), name_id=bindparam('nid'))
    # don't need 'returning id' here so we can use batch mode
    conn.execute(q_insert_cnames, [dict(gid=name_to_gid[cname], nid=name_to_aliasid[cname]) for cname in input_data['gadgets'].keys()])



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
