#include "misc.hh"
#include "store-api.hh"
#include "local-store.hh"
#include "globals.hh"


namespace nix {


Derivation derivationFromPath(StoreAPI & store, const Path & drvPath)
{
    assertStorePath(drvPath);
    store.ensurePath(drvPath);
    return readDerivation(drvPath);
}


void computeFSClosure(StoreAPI & store, const Path & path,
    PathSet & paths, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    if (paths.find(path) != paths.end()) return;
    paths.insert(path);

    PathSet edges;

    if (flipDirection) {
        store.queryReferrers(path, edges);

        if (includeOutputs) {
            PathSet derivers = store.queryValidDerivers(path);
            foreach (PathSet::iterator, i, derivers)
                edges.insert(*i);
        }

        if (includeDerivers && isDerivation(path)) {
            PathSet outputs = store.queryDerivationOutputs(path);
            foreach (PathSet::iterator, i, outputs)
                if (store.isValidPath(*i) && store.queryDeriver(*i) == path)
                    edges.insert(*i);
        }

    } else {
        store.queryReferences(path, edges);

        if (includeOutputs && isDerivation(path)) {
            PathSet outputs = store.queryDerivationOutputs(path);
            foreach (PathSet::iterator, i, outputs)
                if (store.isValidPath(*i)) edges.insert(*i);
        }

        if (includeDerivers) {
            Path deriver = store.queryDeriver(path);
            if (store.isValidPath(deriver)) edges.insert(deriver);
        }
    }

    foreach (PathSet::iterator, i, edges)
        computeFSClosure(store, *i, paths, flipDirection, includeOutputs, includeDerivers);
}


OutputEqClass findOutputEqClass(const Derivation & drv, const string & id)
{
    foreach (DerivationOutputs::const_iterator, i, drv.outputs)
        if (i->first == id) return i->second.eqClass;
    throw Error(format("derivation has no output `%1%'") % id);
}

/* Before consolidating/building, consider all trusted paths in the equivalence classes of the input derivations.  */
PathSet findTrustedEqClassMembers(const OutputEqClass & eqClass,
    const TrustId & trustId)
{
    OutputEqMembers members;
    queryOutputEqMembers(noTxn, eqClass, members);

    PathSet result;
    for (OutputEqMembers::iterator j = members.begin(); j != members.end(); ++j)
        if (j->trustId == trustId || j->trustId == "root") result.insert(j->path);

    return result;
}


Path findTrustedEqClassMember(const OutputEqClass & eqClass,
    const TrustId & trustId)
{
    PathSet paths = findTrustedEqClassMembers(eqClass, trustId);
    if (paths.size() == 0)
        throw Error(format("no output path in equivalence class `%1%' is known") % eqClass);
    return *(paths.begin());
}


typedef map<OutputEqClass, PathSet> ClassMap;
typedef map<OutputEqClass, Path> FinalClassMap;

static void findBestRewrite(const ClassMap::const_iterator & pos,
    const ClassMap::const_iterator & end,
    const PathSet & selection, const PathSet & unselection,
    unsigned int & bestCost, PathSet & bestSelection)
{
    if (pos != end) {
        for (PathSet::iterator i = pos->second.begin();
             i != pos->second.end(); ++i)
        {
            PathSet selection2(selection);
            selection2.insert(*i);
            
            PathSet unselection2(unselection);
            for (PathSet::iterator j = pos->second.begin();
                 j != pos->second.end(); ++j)
                if (i != j) unselection2.insert(*j);
            
            ClassMap::const_iterator j = pos; ++j;
            findBestRewrite(j, end, selection2, unselection2,
                bestCost, bestSelection);
        }
        return;
    }

    PathSet badPaths;
    for (PathSet::iterator i = selection.begin();
         i != selection.end(); ++i)
    {
        PathSet closure;
        computeFSClosure(*i, closure); 
        for (PathSet::iterator j = closure.begin();
             j != closure.end(); ++j)
            if (unselection.find(*j) != unselection.end())
                badPaths.insert(*i);
    }
    
    //    printMsg(lvlError, format("cost %1% %2%") % badPaths.size() % showPaths(badPaths));

    if (badPaths.size() < bestCost) {
        bestCost = badPaths.size();
        bestSelection = selection;
    }
}


static Path maybeRewrite(const Path & path, const PathSet & selection,
    const FinalClassMap & finalClassMap, const PathSet & sources,
    Replacements & replacements,
    unsigned int & nrRewrites)
{
    startNest(nest, lvlError, format("considering rewriting `%1%'") % path);

    assert(selection.find(path) != selection.end());

    if (replacements.find(path) != replacements.end()) return replacements[path];
    
    PathSet references;
    queryReferences(noTxn, path, references);

    HashRewrites rewrites;
    PathSet newReferences;
    
    for (PathSet::iterator i = references.begin(); i != references.end(); ++i) {
        /* Handle sources (which are not in any equivalence class) properly.  */
        /* Also ignore self-references. */
        if (*i == path || sources.find(*i) != sources.end()) {
            newReferences.insert(*i);
            continue;
        }

        OutputEqClasses classes;
        queryOutputEqClasses(noTxn, *i, classes);
        assert(classes.size() > 0);

        FinalClassMap::const_iterator j = finalClassMap.find(*(classes.begin()));
        assert(j != finalClassMap.end());
        
        Path newPath = maybeRewrite(j->second, selection,
            finalClassMap, sources, replacements, nrRewrites);

        if (*i != newPath)
            rewrites[hashPartOf(*i)] = hashPartOf(newPath);

        newReferences.insert(newPath);
    }

    /* Handle the case where all the direct references of a path are in the selection but some indirect reference isn't (in which case the path should still be rewritten). */
    if (rewrites.size() == 0) {
        replacements[path] = path;
        return path;
    }

    printMsg(lvlError, format("rewriting `%1%'") % path);

    Path newPath = addToStore(path,
        hashPartOf(path), namePartOf(path),
        references, rewrites);

    /* Set the equivalence class for paths produced through rewriting. */
    /* !!! we don't know which eqClass `path' is in!  That is to say,
       we don't know which one is intended here. */
    OutputEqClasses classes;
    queryOutputEqClasses(noTxn, path, classes);
    for (PathSet::iterator i = classes.begin(); i != classes.end(); ++i) {
        Transaction txn;
        createStoreTransaction(txn);
        addOutputEqMember(txn, *i, currentTrustId, newPath);
        txn.commit();
    }

    nrRewrites++;

    printMsg(lvlError, format("rewrote `%1%' to `%2%'") % path % newPath);

    replacements[path] = newPath;
        
    return newPath;
}

/*
If we do 
$ NIX_USER_ID=foo nix-env -i libXext $ NIX_USER_ID=root nix-env -i libXt $ NIX_USER_ID=foo nix-env -i libXmu 
(where libXmu depends on libXext and libXt, who both depend on libX11), then the following will happen: 
* User foo builds libX11 and libXext because they don't exist yet. 
* User root builds libX11 and libXt because the latter doesn't exist yet, while the former *does* exist but cannot be trusted. The instance of libX11 built by root will almost certainly differ from the one built by foo, so they are stored in separate locations. 
* User foo builds libXmu, which requires libXext and libXt. Foo has trusted copies of both (libXext was built by himself, while libXt was built by root, who is trusted by foo). So libXmu is built with foo's libXext and root's libXt as inputs. 
* The resulting libXmu will link against two copies of libX11, namely the one used by foo's libXext and the one used by root's libXt. This is bad semantically (it's observable behaviour, and might well lead to build time or runtime failure (e.g., duplicate definitions of symbols)) and in terms of efficiency (the closure of libXmu contains two copies of libX11, so both must be deployed). 
The problem is to apply hash rewriting to "consolidate" the set of input paths to a build. The invariant we wish to maintain is that any closure may contain at most one path from each equivalence class. 
So in the case of a collision, we select one path from each class, and *rewrite* all paths in that set to point only to paths in that set. For instance, in the example above, we can rewrite foo's libXext to link against root's libX11. That is, the hash part of foo's libX11 is replaced by the hash part of root's libX11. 
*/
PathSet consolidatePaths(const PathSet & paths, bool checkOnly,
    Replacements & replacements)
{
    printMsg(lvlError, format("consolidating"));
    
    ClassMap classMap;
    PathSet sources;
    
    for (PathSet::const_iterator i = paths.begin(); i != paths.end(); ++i) {
        OutputEqClasses classes;
        queryOutputEqClasses(noTxn, *i, classes);

        if (classes.size() == 0)
            sources.insert(*i);
        else
            for (OutputEqClasses::iterator j = classes.begin(); j != classes.end(); ++j)
                classMap[*j].insert(*i);
    }

    printMsg(lvlError, format("found %1% sources %2%") % sources.size() % showPaths(sources));

    bool conflict = false;
    for (ClassMap::iterator i = classMap.begin(); i != classMap.end(); ++i)
        if (i->second.size() >= 2) {
            printMsg(lvlError, format("conflict in eq class `%1%'") % i->first);
            conflict = true;
        }

    if (!conflict) return paths;
    
    assert(!checkOnly);

    // The hard part is to figure out which path to select from each class. Some selections may be cheaper than others (i.e., require fewer rewrites). 
    // The current implementation is rather dumb: it tries all possible selections, and picks the cheapest.
    // !!! This is an exponential time algorithm.
    // There certainly are more efficient common-case (heuristic) approaches. But I don't know yet if there is a worst-case polynomial time algorithm. 
    const unsigned int infinity = 1000000;
    unsigned int bestCost = infinity;
    PathSet bestSelection;
    findBestRewrite(classMap.begin(), classMap.end(),
        PathSet(), PathSet(), bestCost, bestSelection);

    assert(bestCost != infinity);

    printMsg(lvlError, format("cheapest selection %1% %2%")
        % bestCost % showPaths(bestSelection));

    FinalClassMap finalClassMap;
    for (ClassMap::iterator i = classMap.begin(); i != classMap.end(); ++i)
        for (PathSet::const_iterator j = i->second.begin(); j != i->second.end(); ++j)
            if (bestSelection.find(*j) != bestSelection.end())
                finalClassMap[i->first] = *j;

    PathSet newPaths;
    unsigned int nrRewrites = 0;
    replacements.clear();
    for (PathSet::iterator i = bestSelection.begin();
         i != bestSelection.end(); ++i)
        newPaths.insert(maybeRewrite(*i, bestSelection, finalClassMap,
                            sources, replacements, nrRewrites));

    newPaths.insert(sources.begin(), sources.end());

    assert(nrRewrites == bestCost);

    assert(newPaths.size() < paths.size());

    return newPaths;
}

void queryMissing(StoreAPI & store, const PathSet & targets,
    PathSet & willBuild, PathSet & willSubstitute, PathSet & unknown,
    unsigned long long & downloadSize, unsigned long long & narSize)
{
    downloadSize = narSize = 0;

    PathSet todo(targets.begin(), targets.end()), done;

    /* Getting substitute info has high latency when using the binary
       cache substituter.  Thus it's essential to do substitute
       queries in parallel as much as possible.  To accomplish this
       we do the following:

       - For all paths still to be processed (‘todo’), we add all
         paths for which we need info to the set ‘query’.  For an
         unbuilt derivation this is the output paths; otherwise, it's
         the path itself.

       - We get info about all paths in ‘query’ in parallel.

       - We process the results and add new items to ‘todo’ if
         necessary.  E.g. if a path is substitutable, then we need to
         get info on its references.

       - Repeat until ‘todo’ is empty.
    */

    while (!todo.empty()) {

        PathSet query, todoDrv, todoNonDrv;

        foreach (PathSet::iterator, i, todo) {
            if (done.find(*i) != done.end()) continue;
            done.insert(*i);

            DrvPathWithOutputs i2 = parseDrvPathWithOutputs(*i);

            if (isDerivation(i2.first)) {
                if (!store.isValidPath(i2.first)) {
                    // FIXME: we could try to substitute p.
                    unknown.insert(*i);
                    continue;
                }
                Derivation drv = derivationFromPath(store, i2.first);

                PathSet invalid;
                foreach (DerivationOutputs::iterator, j, drv.outputs)
                    if (wantOutput(j->first, i2.second)
                        && !store.isValidPath(j->second.path))
                        invalid.insert(j->second.path);
                if (invalid.empty()) continue;

                todoDrv.insert(*i);
                if (settings.useSubstitutes && !willBuildLocally(drv))
                    query.insert(invalid.begin(), invalid.end());
            }

            else {
                if (store.isValidPath(*i)) continue;
                query.insert(*i);
                todoNonDrv.insert(*i);
            }
        }

        todo.clear();

        SubstitutablePathInfos infos;
        store.querySubstitutablePathInfos(query, infos);

        foreach (PathSet::iterator, i, todoDrv) {
            DrvPathWithOutputs i2 = parseDrvPathWithOutputs(*i);

            // FIXME: cache this
            Derivation drv = derivationFromPath(store, i2.first);

            PathSet outputs;
            bool mustBuild = false;
            if (settings.useSubstitutes && !willBuildLocally(drv)) {
                foreach (DerivationOutputs::iterator, j, drv.outputs) {
                    if (!wantOutput(j->first, i2.second)) continue;
                    if (!store.isValidPath(j->second.path)) {
                        if (infos.find(j->second.path) == infos.end())
                            mustBuild = true;
                        else
                            outputs.insert(j->second.path);
                    }
                }
            } else
                mustBuild = true;

            if (mustBuild) {
                willBuild.insert(i2.first);
                todo.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
                foreach (DerivationInputs::iterator, j, drv.inputDrvs)
                    todo.insert(makeDrvPathWithOutputs(j->first, j->second));
            } else
                todoNonDrv.insert(outputs.begin(), outputs.end());
        }

        foreach (PathSet::iterator, i, todoNonDrv) {
            done.insert(*i);
            SubstitutablePathInfos::iterator info = infos.find(*i);
            if (info != infos.end()) {
                willSubstitute.insert(*i);
                downloadSize += info->second.downloadSize;
                narSize += info->second.narSize;
                todo.insert(info->second.references.begin(), info->second.references.end());
            } else
                unknown.insert(*i);
        }
    }
}


static void dfsVisit(StoreAPI & store, const PathSet & paths,
    const Path & path, PathSet & visited, Paths & sorted,
    PathSet & parents)
{
    if (parents.find(path) != parents.end())
        throw BuildError(format("cycle detected in the references of `%1%'") % path);

    if (visited.find(path) != visited.end()) return;
    visited.insert(path);
    parents.insert(path);

    PathSet references;
    if (store.isValidPath(path))
        store.queryReferences(path, references);

    foreach (PathSet::iterator, i, references)
        /* Don't traverse into paths that don't exist.  That can
           happen due to substitutes for non-existent paths. */
        if (*i != path && paths.find(*i) != paths.end())
            dfsVisit(store, paths, *i, visited, sorted, parents);

    sorted.push_front(path);
    parents.erase(path);
}


Paths topoSortPaths(StoreAPI & store, const PathSet & paths)
{
    Paths sorted;
    PathSet visited, parents;
    foreach (PathSet::const_iterator, i, paths)
        dfsVisit(store, paths, *i, visited, sorted, parents);
    return sorted;
}


}
