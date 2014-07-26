/* Path hashes are the hash components of store paths, e.g., the
   `zvhgns772jpj68l40mq1jb74wpfsf0ma' in
   `/nix/store/zvhgns772jpj68l40mq1jb74wpfsf0ma-glibc'.  These are
   truncated SHA-256 hashes of the path contents,  */
struct PathHash
{
private:
    string rep;
public:
    PathHash();
    PathHash(const Hash & h);
    PathHash(const string & h);
    string toString() const;
    bool PathHash::isNull() const;
    bool operator ==(const PathHash & hash2) const;
    bool operator <(const PathHash & hash2) const;
};
