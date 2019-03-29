import argparse
import os
import pwd
from sqlalchemy import create_engine
from . import models
from . import sync_known_gadgets, unary, combine, compact_completion

parser = argparse.ArgumentParser()
subparsers = parser.add_subparsers()

parser.add_argument('--runner-path', type=str, help='Path to local toggles-runner executable', default='build/debug/bin/toggles-runner.exe')

dbopts = parser.add_argument_group('Database options')
dbopts.add_argument('--db-user', '--db-username', type=str, default=pwd.getpwuid(os.getuid()).pw_name)
dbopts.add_argument('--db-pass', '--db-password', type=str, default='')
dbopts.add_argument('--db-host', '--db-hostname', type=str, default='127.0.0.1')
dbopts.add_argument('--db-port', type=int, default=5432)
dbopts.add_argument('--db-name', type=str, default='togglesearch')

for module in (sync_known_gadgets, unary, combine, compact_completion):
    module.register_subcommand(parser, subparsers)

args = parser.parse_args()

connect_url = 'postgresql+psycopg2://{db_user}:{db_pass}@{db_host}:{db_port}/{db_name}'.format(**vars(args))
engine = create_engine(connect_url, isolation_level='SERIALIZABLE')
models.Session.configure(bind=engine)
models.Base.metadata.create_all(engine)
# Until https://github.com/sqlalchemy/sqlalchemy/issues/4458 is implemented,
# we have to do this manually after calling create_all.
with models.session_scope() as s:
    s.execute('create index if not exists combine_edges_follow on combine_edges(input1, input2) include(output1)')
    s.execute('create index if not exists connect_edges_follow on connect_edges(input1) include(output1)')
    s.execute('create index if not exists mirror_edges_follow_a_b on mirror_edges(a) include(b)')
    s.execute('create index if not exists mirror_edges_follow_b_a on mirror_edges(b) include(a)')

args.command_func(args)
