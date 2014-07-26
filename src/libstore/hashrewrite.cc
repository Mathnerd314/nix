/*
* Any component in the Nix store resides has a store path name that has a hash component equal to the hash of the contents of that component, i.e., hashPartOf(path) = hashOf(contentsAt(path)). E.g., a path /nix/store/nc35k7yr8...-foo would have content hash nc35k7yr8... When building components in the Nix store, we don't know the content hash until after the component has been built. We handle this by building the component at some randomly generated prefix in the Nix store, and then afterwards *rewriting* the random prefix to the hash of the actual contents. If components that reference themselves, such as ELF executables that contain themselves in their RPATH, we compute content hashes "modulo" the original prefix, i.e., we zero out every occurence of the randomly generated prefix, compute the content hash, then rewrite the random prefix to the final location.
* Take the position of self-references into account when computing content hashes. This is to prevent a rewrite of 
...HASH...HASH... 
and 
...HASH...0000... 
(where HASH is the randomly generated prefix) from hashing to the same value. This would happen because they would both resolve to ...0000...0000... Exploiting this into a security hole is left as an exercise to the reader ;-) 
*/

//////////////////////////////////////////////////////////////////////


typedef map<string, string> HashRewrites;


string rewriteHashes(string s, const HashRewrites & rewrites)
{
    foreach (HashRewrites::const_iterator, i, rewrites) {
        assert(i->first.size() == i->second.size());
        size_t j = 0;
        while ((j = s.find(i->first, j)) != string::npos) {
            debug(format("rewriting @ %1%") % j);
            s.replace(j, i->second.size(), i->second);
        }
    }
    return s;
}


//////////////////////////////////////////////////////////////////////

typedef map<PathHash, PathHash> HashRewrites;

string rewriteHashes(string s, const HashRewrites & rewrites,
    vector<int> & positions)
{
    for (HashRewrites::const_iterator i = rewrites.begin();
         i != rewrites.end(); ++i)
    {
        string from = i->first.toString(), to = i->second.toString();

        assert(from.size() == to.size());

        unsigned int j = 0;
        while ((j = s.find(from, j)) != string::npos) {
            debug(format("rewriting @ %1%") % j);
            positions.push_back(j);
            s.replace(j, to.size(), to);
            j += to.size();
        }
    }

    return s;
}


string rewriteHashes(const string & s, const HashRewrites & rewrites)
{
    vector<int> dummy;
    return rewriteHashes(s, rewrites, dummy);
}


static Hash hashModulo(string s, const PathHash & modulus)
{
    vector<int> positions;
    
    if (!modulus.isNull()) {
        /* Zero out occurences of `modulus'. */
        HashRewrites rewrites;
        rewrites[modulus] = PathHash(); /* = null hash */
        s = rewriteHashes(s, rewrites, positions);
    }

    string positionPrefix;
    
    for (vector<int>::iterator i = positions.begin();
         i != positions.end(); ++i)
        positionPrefix += (format("|%1%") % *i).str();

    positionPrefix += "||";

    debug(format("positions %1%") % positionPrefix);
    
    return hashString(htSHA256, positionPrefix + s);
}


static PathSet rewriteReferences(const PathSet & references,
    const HashRewrites & rewrites)
{
    PathSet result;
    for (PathSet::const_iterator i = references.begin(); i != references.end(); ++i)
        result.insert(rewriteHashes(*i, rewrites));
    return result;
}


