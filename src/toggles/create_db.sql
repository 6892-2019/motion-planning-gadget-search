-- serializable
alter database togglesearch set default_transaction_isolation = "serializable";

create table gadgets (
	id bigint primary key generated always as identity,
	states smallint not null,
	locations smallint not null,
	undirected_edges smallint not null,
	directed_edges smallint not null,
	-- thus total edges is 2*undirected_edges + directed edges, may want an index on that?
	-- May have some properties like 'deterministic', 'on lines' etc.  We'll put
	-- those in separate tables because we'll probably want to add more such
	-- features later and/or compute for only a subset of the gadgets.
	-- this comes last because it's variably-sized
	edges bytea not null -- TODO: index over hash expression? don't use 'unique' because that creates a btree, copying all the data
);

-- some columns may be null for connects (and, if implemented, deletes)
create table provenance (
	id bigint primary key generated always as identity,
	g1 bigint references gadgets not null,
	g2 bigint references gadgets,
	splice smallint,
	rotation smallint,
	location smallint not null,
	-- TODO: Whether we choose a component index or state number, we didn't
	-- compute this value from a canonical automaton, so it's fragile.
	root smallint not null -- could make this null if there was only one component, I guess
);

create table completed_combines (
	id bigint primary key generated always as identity,
	g1 bigint references gadgets not null,
	g2 bigint references gadgets not null,
	alphabet_size smallint not null
);

-- Stores closed intervals
create table completed_connects (
	id bigint primary key generated always as identity,
	r int8range not null check(lower_inc(r) and upper_inc(r)),
	-- Unfortunately we can't reference the gadgets table in a check constraint,
	-- so the best we can do is ensure the ranges don't overlap.  (Another
	-- option would be to use two foreign key columns and index on an expression.)
	exclude using gist (r with &&)
);

-- Human-readable names for gadgets.
create table aliases (
	id bigint primary key generated always as identity,
	gadget_id bigint references gadgets not null,
	name text unique not null
);

-- Canonical names for gadgets.  This is the name used for reports.  If a gadget
-- has one or more names but isn't listed here, reports may not use it, or may
-- pick a name arbitrarily.
create table cnames (
	id bigint primary key generated always as identity,
	gadget_id bigint references gadgets unique not null,
	name_id bigint references aliases unique not null
);

-- We could also have a table of gadget sets, but we probably actually want to
-- use a query (e.g., all n-state m-location deterministic gadgets).  (The
-- disadvantage of queries is that their membership depends on the contents of
-- the database, expanding as more gadgets are discovered.)