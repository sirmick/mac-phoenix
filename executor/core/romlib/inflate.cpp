/* MacPhoenix: zlib streams inflated (RFC 1950, "ZLIB Compressed Data
 * Format", around RFC 1951, "DEFLATE Compressed Data Format"), written from
 * the RFCs. QuickTime 3 compresses a movie's header so: a 'cmov' atom
 * holding 'dcom' ('zlib') and 'cmvd' (the inflated size, then the zlib
 * stream; movies.cpp).
 *
 * Huffman codes are decoded canonically: for each code length the count
 * of codes and the first code, the symbols in code order (RFC 1951 3.2.2). */
#include <rsys/inflate.h>

#include <cstring>

namespace
{
struct Bits
{
    const uint8_t *p, *end;
    uint32_t buf = 0;
    int have = 0;
    bool bad = false;

    int bit()
    {
        if(!have)
        {
            if(p == end)
            {
                bad = true;
                return 0;
            }
            buf = *p++;
            have = 8;
        }
        int b = buf & 1;
        buf >>= 1;
        have--;
        return b;
    }
    /* n bits, the first one read the least significant */
    uint32_t bits(int n)
    {
        uint32_t v = 0;
        for(int i = 0; i < n; i++)
            v |= (uint32_t)bit() << i;
        return v;
    }
    void to_byte()
    {
        have = 0;
    }
};

struct Huffman
{
    uint16_t count[16];  /* codes of each length */
    uint16_t symbol[320]; /* the symbols, by length then code */

    /* false: more codes of a length than the length allows */
    bool build(const uint8_t *lengths, int n)
    {
        memset(count, 0, sizeof count);
        for(int i = 0; i < n; i++)
            count[lengths[i]]++;
        count[0] = 0;
        int left = 1;
        for(int len = 1; len < 16; len++)
        {
            left <<= 1;
            left -= count[len];
            if(left < 0)
                return false;
        }
        uint16_t offset[16];
        offset[1] = 0;
        for(int len = 1; len < 15; len++)
            offset[len + 1] = offset[len] + count[len];
        for(int i = 0; i < n; i++)
            if(lengths[i])
                symbol[offset[lengths[i]]++] = i;
        return true;
    }

    /* the next symbol; -1 for a code that isn't there */
    int decode(Bits &in) const
    {
        int code = 0, first = 0, index = 0;
        for(int len = 1; len < 16; len++)
        {
            code |= in.bit();
            int n = count[len];
            if(code - first < n)
                return symbol[index + code - first];
            index += n;
            first = (first + n) << 1;
            code <<= 1;
            if(in.bad)
                return -1;
        }
        return -1;
    }
};

const uint16_t length_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                 6145, 8193, 12289, 16385, 24577 };
const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* one block's literals and copies, up to its end-of-block */
bool codes(Bits &in, const Huffman &lit, const Huffman &dist, std::vector<uint8_t> &out, size_t limit)
{
    for(;;)
    {
        int sym = lit.decode(in);
        if(sym < 0 || in.bad)
            return false;
        if(sym < 256)
        {
            if(out.size() >= limit)
                return false;
            out.push_back(sym);
            continue;
        }
        if(sym == 256)
            return true;
        sym -= 257;
        if(sym >= 29)
            return false;
        size_t len = length_base[sym] + in.bits(length_extra[sym]);
        int d = dist.decode(in);
        if(d < 0 || d >= 30)
            return false;
        size_t back = dist_base[d] + in.bits(dist_extra[d]);
        if(in.bad || back > out.size() || out.size() + len > limit)
            return false;
        size_t from = out.size() - back;
        for(size_t i = 0; i < len; i++)
            out.push_back(out[from + i]);
    }
}

bool fixed_block(Bits &in, std::vector<uint8_t> &out, size_t limit)
{
    static Huffman lit, dist;
    static bool made = false;
    if(!made)
    {
        uint8_t l[288];
        int i = 0;
        for(; i < 144; i++)
            l[i] = 8;
        for(; i < 256; i++)
            l[i] = 9;
        for(; i < 280; i++)
            l[i] = 7;
        for(; i < 288; i++)
            l[i] = 8;
        lit.build(l, 288);
        uint8_t d[30];
        memset(d, 5, sizeof d);
        dist.build(d, 30);
        made = true;
    }
    return codes(in, lit, dist, out, limit);
}

bool dynamic_block(Bits &in, std::vector<uint8_t> &out, size_t limit)
{
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen = in.bits(5) + 257, ndist = in.bits(5) + 1, ncode = in.bits(4) + 4;
    if(nlen > 286 || ndist > 30)
        return false;
    uint8_t lengths[320] = {};
    for(int i = 0; i < ncode; i++)
        lengths[order[i]] = in.bits(3);
    Huffman lencode;
    if(!lencode.build(lengths, 19))
        return false;
    int i = 0;
    while(i < nlen + ndist)
    {
        int sym = lencode.decode(in);
        if(sym < 0 || in.bad)
            return false;
        if(sym < 16)
        {
            lengths[i++] = sym;
            continue;
        }
        int len = 0, rep;
        if(sym == 16)
        {
            if(i == 0)
                return false;
            len = lengths[i - 1];
            rep = 3 + in.bits(2);
        }
        else if(sym == 17)
            rep = 3 + in.bits(3);
        else
            rep = 11 + in.bits(7);
        if(i + rep > nlen + ndist)
            return false;
        while(rep--)
            lengths[i++] = len;
    }
    if(lengths[256] == 0)
        return false;
    Huffman lit, dist;
    if(!lit.build(lengths, nlen) || !dist.build(lengths + nlen, ndist))
        return false;
    return codes(in, lit, dist, out, limit);
}

uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    for(size_t i = 0; i < n; i++)
    {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}
}

bool Executor::ROMlib_zlib_inflate(const uint8_t *p, size_t n, std::vector<uint8_t> &out, size_t limit)
{
    out.clear();
    /* CMF (method 8, a window of at most 32K), FLG (no preset dictionary),
       the two a multiple of 31 */
    if(n < 6 || (p[0] & 0x0F) != 8 || (p[0] >> 4) > 7 || (p[1] & 0x20) || ((p[0] << 8) | p[1]) % 31)
        return false;
    Bits in{ p + 2, p + n - 4 };
    int last;
    do
    {
        last = in.bit();
        int type = in.bits(2);
        bool ok;
        if(type == 0)
        {
            in.to_byte();
            if(in.end - in.p < 4)
                return false;
            unsigned len = in.p[0] | (in.p[1] << 8), nlen = in.p[2] | (in.p[3] << 8);
            in.p += 4;
            if(len != (~nlen & 0xFFFF) || (size_t)(in.end - in.p) < len || out.size() + len > limit)
                return false;
            out.insert(out.end(), in.p, in.p + len);
            in.p += len;
            ok = true;
        }
        else if(type == 1)
            ok = fixed_block(in, out, limit);
        else if(type == 2)
            ok = dynamic_block(in, out, limit);
        else
            ok = false;
        if(!ok || in.bad)
            return false;
    } while(!last);
    const uint8_t *a = p + n - 4;
    uint32_t want = ((uint32_t)a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
    return adler32(out.data(), out.size()) == want;
}
