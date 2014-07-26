#pragma once

#include "derivations.hh"


namespace nix {


/* Read a derivation, after ensuring its existence through
   ensurePath(). */
Derivation derivationFromPath(StoreAPI & store, const Path & drvPath);

/* Place in `paths' the set of all store paths in the file system
   closure of `storePath'; that is, all paths than can be directly or
   indirectly reached from it.  `paths' is not cleared.  If
   `flipDirection' is true, the set of paths that can reach
   `storePath' is returned; that is, the closures under the
   `referrers' relation instead of the `references' relation is
   returned. */
void computeFSClosure(StoreAPI & store, const Path & path,
    PathSet & paths, bool flipDirection = false,
    bool includeOutputs = false, bool includeDerivers = false);

/* Return the path corresponding to the output identifier `id' in the
   given derivation. */
Path findOutput(const Derivation & drv, string id);

/* Return the output equivalence class denoted by `id' in the
   derivation `drv'. */
OutputEqClass findOutputEqClass(const Derivation & drv,
    const string & id);

/* Return anll trusted path (wrt to the given trust ID) in the given
   output path equivalence class, or an empty set if no such paths
   currently exist. */
PathSet findTrustedEqClassMembers(const OutputEqClass & eqClass,
    const TrustId & trustId);

/* Like `findTrustedEqClassMembers', but returns an arbitrary trusted
   path, or throws an exception if no such path currently exists. */
Path findTrustedEqClassMember(const OutputEqClass & eqClass,
    const TrustId & trustId);

typedef map<Path, Path> Replacements;

/* Equivalence class consolidation. This solves the problem that when we combine closures built by different users, the resulting set may contain multiple paths from the same output path equivalence class. */
PathSet consolidatePaths(const PathSet & paths, bool checkOnly,
    Replacements & replacements);

/* Given a set of paths that are to be built, return the set of
   derivations that will be built, and the set of output paths that
   will be substituted. */
void queryMissing(StoreAPI & store, const PathSet & targets,
    PathSet & willBuild, PathSet & willSubstitute, PathSet & unknown,
    unsigned long long & downloadSize, unsigned long long & narSize);

bool willBuildLocally(const Derivation & drv);


}
