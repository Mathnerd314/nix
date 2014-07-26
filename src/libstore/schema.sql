create table if not exists ValidPaths (
    id               integer primary key autoincrement not null,
    path             text unique not null,
    hash             text not null,
    registrationTime integer not null,
    deriver          text,
    narSize          integer
);

create table if not exists Refs (
    referrer  integer not null,
    reference integer not null,
    primary key (referrer, reference),
    foreign key (referrer) references ValidPaths(id) on delete cascade,
    foreign key (reference) references ValidPaths(id) on delete restrict
);

create index if not exists IndexReferrer on Refs(referrer);
create index if not exists IndexReference on Refs(reference);

-- Paths can refer to themselves, causing a tuple (N, N) in the Refs
-- table.  This causes a deletion of the corresponding row in
-- ValidPaths to cause a foreign key constraint violation (due to `on
-- delete restrict' on the `reference' column).  Therefore, explicitly
-- get rid of self-references.
create trigger if not exists DeleteSelfRefs before delete on ValidPaths
  begin
    delete from Refs where referrer = old.id and reference = old.id;
  end;

/*
create table if not exists DerivationOutputs (
    drv  integer not null,
    id   text not null, -- symbolic output id, usually "out"
    path text not null,
    primary key (drv, id),
    foreign key (drv) references ValidPaths(id) on delete cascade
);

create index if not exists IndexDerivationOutputs on DerivationOutputs(path);

create table if not exists FailedPaths (
    path text primary key not null,
    time integer not null
);
*/

/* dbEquivalences :: OutputEqClass -> [(TrustId, Path)]

   Lists the output paths that have been produced for each extension
   class; i.e., the extension of an extension class. */
static TableId dbEquivalences = 0;

/* dbEquivalenceClasses :: Path -> [OutputEqClass]

   !!! should be [(TrustId, OutputEqClass)] ?

   Lists for each output path the extension classes that it is in. */
static TableId dbEquivalenceClasses = 0;
