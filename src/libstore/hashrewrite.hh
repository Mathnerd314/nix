/* Hash rewriting. */
typedef map<PathHash, PathHash> HashRewrites;

string rewriteHashes(string s, const HashRewrites & rewrites,
    vector<int> & positions);

string rewriteHashes(const string & s, const HashRewrites & rewrites);
