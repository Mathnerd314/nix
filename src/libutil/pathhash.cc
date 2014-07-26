/* Path hashes. */

const unsigned int pathHashLen = 32; /* characters */
const string nullPathHashRef(pathHashLen, 0);


PathHash::PathHash()
{
    rep = nullPathHashRef;
}


PathHash::PathHash(const Hash & h)
{
    assert(h.type == htSHA256);
    rep = printHash32(compressHash(h, 20));
}


PathHash::PathHash(const string & h)
{
    /* !!! hacky; check whether this is a valid 160 bit hash */
    assert(h.size() == pathHashLen);
    parseHash32(htSHA1, h); 
    rep = h;
}


string PathHash::toString() const
{
    return rep;
}


bool PathHash::isNull() const
{
    return rep == nullPathHashRef;
}


bool PathHash::operator ==(const PathHash & hash2) const
{
    return rep == hash2.rep;
}


bool PathHash::operator <(const PathHash & hash2) const
{
    return rep < hash2.rep;
}
