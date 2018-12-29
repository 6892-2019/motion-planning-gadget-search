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

create table combine_provenance (
	id bigint primary key generated always as identity,
	input1 bigint references gadgets not null,
	input2 bigint references gadgets not null,
	output1 bigint references gadgets not null,
	-- The smallest accepting state in the chosen component.  (This is fragile
	-- because it doesn't come from a canonical automaton, but slightly less
	-- fragile than just taking the component number.)
	root integer not null,
	splice smallint not null,
	rotation smallint not null,
	location smallint not null
);

create table connect_provenance (
	id bigint primary key generated always as identity,
	input1 bigint references gadgets not null,
	output1 bigint references gadgets not null,
	location smallint not null
	-- TODO: could replace the primary key with a composite key of all three
	-- fields; could also do the same for combine_provenance if we really wanted (all fields)
);

create table mirror_provenance (
	a bigint references gadgets not null,
	b bigint references gadgets not null,
	primary key (a, b),
	check(a < b)
	-- TODO: prevent (a, c) and (b, c)/(a, b) and (a, c) from coexisting (enforce it's a partial matching)
);

create table completed_combines (
	-- no specific primary key column as we should only keep the high-water mark for each pair
	g1 bigint references gadgets not null,
	g2 bigint references gadgets not null,
	alphabet_size smallint not null,
	primary key (g1, g2)
);

-- Stores closed intervals
create table completed_connects (
	id bigint primary key generated always as identity,
	r int8range not null check(lower_inc(r) and not upper_inc(r)),
	-- Unfortunately we can't reference the gadgets table in a check constraint,
	-- so the best we can do is ensure the ranges don't overlap.  (Another
	-- option would be to use two foreign key columns and index on an expression.)
	exclude using gist (r with &&)
);

create table completed_mirrors (
	id bigint primary key generated always as identity,
	r int8range not null check(lower_inc(r) and not upper_inc(r)),
	exclude using gist (r with &&)
);

-- No specific chirality table: if a gadget's id is in mirror_provenance, it's
-- chiral; if not and its id is in completed_mirrors, it's achiral, and
-- otherwise we don't know.

-- Human-readable names for gadgets.  A name may map to multiple gadgets.
create table aliases (
	id bigint primary key generated always as identity,
	gadget_id bigint references gadgets not null,
	name text not null,
	unique (gadget_id, name)
);

-- Canonical names for gadgets.  This is the name used for reports.  If a gadget
-- has one or more names but isn't listed here, reports may not use it, or may
-- pick a name arbitrarily.  Canonical names cannot name alias groups (because
-- their purpose is to name a single gadget).
create table cnames (
	id bigint primary key generated always as identity,
	gadget_id bigint references gadgets unique not null,
	name text unique not null
);

-- We could also have a table of gadget sets, but we probably actually want to
-- use a query (e.g., all n-state m-location deterministic gadgets).  (The
-- disadvantage of queries is that their membership depends on the contents of
-- the database, expanding as more gadgets are discovered.)