from contextlib import contextmanager
from typing import ContextManager

import sqlalchemy
from sqlalchemy import Column, Integer, Index, BigInteger, ForeignKey, SmallInteger, CheckConstraint, Text
from sqlalchemy.dialects.postgresql import BYTEA, INT8RANGE, ExcludeConstraint
from sqlalchemy.ext.declarative import declarative_base
from psycopg2.extras import NumericRange


# https://docs.sqlalchemy.org/en/rel_1_2/dialects/postgresql.html#postgresql-10-identity-columns
from sqlalchemy.orm import sessionmaker
from sqlalchemy.schema import CreateColumn
from sqlalchemy.ext.compiler import compiles
@compiles(CreateColumn, 'postgresql')
def use_identity(element, compiler, **kw):
    text = compiler.visit_create_column(element, **kw)
    text = text.replace("SERIAL", "INT GENERATED ALWAYS AS IDENTITY")
    return text


Base = declarative_base()


class Gadget(Base):
    __tablename__ = 'gadgets'
    id = Column('id', BigInteger, primary_key=True, nullable=False)
    states = Column('states', Integer, nullable=False)
    locations = Column('locations', Integer, nullable=False)
    undirected_edges = Column('undirected_edges', Integer, nullable=False)
    directed_edges = Column('directed_edges', Integer, nullable=False)
    components = Column('components', Integer, nullable=False)
    data = Column('data', BYTEA, nullable=False)

    # We'd make this a unique index but postgres doesn't support that yet.
    __table_args__ = (Index('idx_gadgets_data', data, postgresql_using='hash'),)

    @classmethod
    def from_tuple(cls, t):
        if len(t) != 6:
            raise ValueError('bad tuple length {} {}'.format(len(t), t))
        return Gadget(states=t[0], locations=t[1], undirected_edges=t[2],
                directed_edges=t[3], components=t[4], data=t[5])


class CombineEdge(Base):
    __tablename__ = 'combine_edges'
    id = Column('id', BigInteger, primary_key=True, nullable=False)
    input1 = Column('input1', BigInteger, ForeignKey(Gadget.id), nullable=False)
    input2 = Column('input2', BigInteger, ForeignKey(Gadget.id), nullable=False)
    output1 = Column('output1', BigInteger, ForeignKey(Gadget.id), nullable=False)
    splice = Column('splice', SmallInteger, nullable=False)
    rotation = Column('rotation', SmallInteger, nullable=False)
    connect_location = Column('connect_location', SmallInteger, nullable=False)
    canonicalize_rotation = Column('canonicalize_rotation', SmallInteger, nullable=False)


class ConnectEdge(Base):
    __tablename__ = 'connect_edges'
    id = Column('id', BigInteger, primary_key=True, nullable=False)
    input1 = Column('input1', BigInteger, ForeignKey(Gadget.id), nullable=False)
    output1 = Column('output1', BigInteger, ForeignKey(Gadget.id), nullable=False)
    connect_location = Column('connect_location', SmallInteger, nullable=False)
    canonicalize_rotation = Column('canonicalize_rotation', SmallInteger, nullable=False)


class MirrorEdge(Base):
    __tablename__ = 'mirror_edges'
    a = Column('a', BigInteger, ForeignKey(Gadget.id), primary_key=True, nullable=False)
    b = Column('b', BigInteger, ForeignKey(Gadget.id), primary_key=True, nullable=False)
    # TODO: this doesn't prevent (a,c)/(b,c); we want this table to be a (partial) matching
    __table_args__ = (CheckConstraint('a < b', name='chk_mirror_sorted'),)


class CompletedCombine(Base):
    __tablename__ = 'completed_combines'
    input1 = Column('input1', BigInteger, ForeignKey(Gadget.id), primary_key=True, nullable=False)
    input2 = Column('input2', BigInteger, ForeignKey(Gadget.id), primary_key=True, nullable=False)
    # max useful precision is input1.locations + input2.locations
    precision = Column('precision', SmallInteger, nullable=False)


class CompletedConnect(Base):
    __tablename__ = 'completed_connectss'
    id = Column('id', BigInteger, primary_key=True, nullable=False)
    r = Column('r', INT8RANGE, CheckConstraint('lower_inc(r) and not upper_inc(r)'), nullable=False)
    __table_args__ = (ExcludeConstraint(('r', '&&'), name='exc_compconnect_r'),)

    @classmethod
    def singleton(cls, gadget_id):
        return CompletedConnect(r=NumericRange(lower=gadget_id, upper=gadget_id+1))


class CompletedMirror(Base):
    __tablename__ = 'completed_mirrors'
    id = Column('id', BigInteger, primary_key=True, nullable=False)
    r = Column('r', INT8RANGE, CheckConstraint('lower_inc(r) and not upper_inc(r)'), nullable=False)
    __table_args__ = (ExcludeConstraint(('r', '&&'), name='exc_compmirror_r'),)

    @classmethod
    def singleton(cls, gadget_id):
        return CompletedMirror(r=NumericRange(lower=gadget_id, upper=gadget_id + 1))


# No specific chirality table; if a gadget's id is in mirror_edges, it's chiral;
# if not and its id is contained within completed_mirrors, it's a chiral;
# otherwise, we don't yet know.


# Human-readable names for gadgets.  A name may map to multiple gadgets and a
# gadget may have multiple names.  When listing produced gadgets, we will use
# every name that names only that gadget.
class Name(Base):
    __tablename__ = 'names'
    id = Column('id', BigInteger, primary_key=True, nullable=False)
    gadget_id = Column('gadget_id', BigInteger, ForeignKey(Gadget.id), nullable=False)
    name = Column('name', Text, nullable=False)


Session = sessionmaker()
# https://docs.sqlalchemy.org/en/rel_1_2/orm/session_basics.html#basics-of-using-a-session
@contextmanager
def session_scope() -> ContextManager[sqlalchemy.orm.session.Session]:
    """Provide a transactional scope around a series of operations."""
    session = Session()
    try:
        yield session
        session.commit()
    except:
        session.rollback()
        raise
    finally:
        session.close()
