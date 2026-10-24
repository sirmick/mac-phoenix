#include "btree.h"

#include <cstring>

using namespace Executor::hfsplus;

namespace
{
enum : int8_t
{
    kLeafNode = -1,
    kIndexNode = 0,
    kHeaderNode = 1,
    kMapNode = 2,
};
enum : uint32_t
{
    kBTBigKeys = 1u << 1,
    kBTVariableIndexKeys = 1u << 2,
};
const size_t kDescriptor = 14;
} // namespace

/* ---- reading -------------------------------------------------------------- */

bool BTree::open()
{
    uint8_t h[512];
    if(file_.read(0, h, sizeof h) != sizeof h || int8_t(h[8]) != kHeaderNode)
        return false;
    const uint8_t *r = h + kDescriptor;
    depth_ = be16(r);
    root_ = be32(r + 2);
    leafRecords_ = be32(r + 6);
    firstLeaf_ = be32(r + 10);
    lastLeaf_ = be32(r + 14);
    nodeSize_ = be16(r + 18);
    maxKeyLength_ = be16(r + 20);
    totalNodes_ = be32(r + 22);
    freeNodes_ = be32(r + 26);
    clumpSize_ = be32(r + 32);
    keyCompareType_ = r[37];
    attributes_ = be32(r + 38);
    return nodeSize_ >= 512 && (nodeSize_ & (nodeSize_ - 1)) == 0;
}

BTree::Node BTree::node(uint32_t n)
{
    auto it = index_.find(n);
    if(it != index_.end())
    {
        lru_.splice(lru_.begin(), lru_, it->second);
        return it->second->second;
    }
    auto buf = std::make_shared<std::vector<uint8_t>>(nodeSize_);
    if(file_.read(uint64_t(n) * nodeSize_, buf->data(), nodeSize_) != nodeSize_)
        return nullptr;
    lru_.emplace_front(n, buf);
    index_[n] = lru_.begin();
    if(lru_.size() > 64)
    {
        index_.erase(lru_.back().first);
        lru_.pop_back();
    }
    return buf;
}

size_t BTree::keyBytes(const uint8_t *key, bool index) const
{
    size_t n = (index && !(attributes_ & kBTVariableIndexKeys)) ? 2 + maxKeyLength_ : 2 + be16(key);
    return (n + 1) & ~size_t(1);
}

bool BTree::record(const Node& n, uint16_t i, const uint8_t **key, const uint8_t **data, size_t *room)
{
    if(i >= count(n))
        return false;
    const uint8_t *base = n->data();
    uint16_t off = be16(base + nodeSize_ - 2 * (i + 1));
    if(off < kDescriptor || off + 2 > nodeSize_)
        return false;
    size_t kb = keyBytes(base + off, kind(n) == kIndexNode);
    if(off + kb > nodeSize_)
        return false;
    *key = base + off;
    *data = base + off + kb;
    *room = nodeSize_ - (off + kb);
    return true;
}

bool BTree::seek(const Compare& cmp, Position *pos)
{
    uint32_t n = root_;
    for(int guard = 0; n && guard < 32; guard++)
    {
        Node nd = node(n);
        if(!nd)
            return false;
        const uint8_t *key, *data;
        size_t room;
        if(kind(nd) == kLeafNode)
        {
            uint16_t i = 0;
            for(; i < count(nd); i++)
                if(record(nd, i, &key, &data, &room) && cmp(key) <= 0)
                    break;
            pos->node = n;
            pos->record = i;
            return true;
        }
        if(kind(nd) != kIndexNode)
            return false;
        /* the last record whose key is <= the one sought (or the first) */
        uint32_t child = 0;
        for(uint16_t i = 0; i < count(nd); i++)
        {
            if(!record(nd, i, &key, &data, &room) || room < 4)
                return false;
            if(i > 0 && cmp(key) < 0)
                break;
            child = be32(data);
        }
        n = child;
    }
    return false;
}

bool BTree::at(Position *pos, const uint8_t **key, const uint8_t **data, size_t *room, Node *keep)
{
    for(int guard = 0; guard < 1 << 20; guard++)
    {
        if(!pos->node)
            return false;
        Node nd = node(pos->node);
        if(!nd || kind(nd) != kLeafNode)
            return false;
        if(pos->record < count(nd))
        {
            *keep = nd;
            return record(nd, pos->record, key, data, room);
        }
        pos->node = next(nd);
        pos->record = 0;
    }
    return false;
}

/* ---- writing: nodes ----------------------------------------------------- */

std::vector<BTree::Record> BTree::records(const Node& n)
{
    std::vector<Record> out;
    const uint8_t *base = n->data();
    uint16_t c = count(n);
    for(uint16_t i = 0; i < c; i++)
    {
        uint16_t a = be16(base + nodeSize_ - 2 * (i + 1)), b = be16(base + nodeSize_ - 2 * (i + 2));
        if(a < kDescriptor || b < a || b > nodeSize_)
            break;
        out.emplace_back(base + a, base + b);
    }
    return out;
}

std::vector<uint8_t> BTree::indexKey(const uint8_t *key) const
{
    if(attributes_ & kBTVariableIndexKeys)
    {
        std::vector<uint8_t> k(key, key + 2 + be16(key));
        if(k.size() & 1)
            k.push_back(0);
        return k;
    }
    std::vector<uint8_t> k(2 + maxKeyLength_, 0);
    memcpy(k.data(), key, std::min<size_t>(k.size(), 2 + be16(key)));
    put16(k.data(), maxKeyLength_);
    return k;
}

BTree::Record BTree::indexRecord(const Record& first, uint32_t child) const
{
    Record r = indexKey(first.data());
    uint8_t p[4];
    put32(p, child);
    r.insert(r.end(), p, p + 4);
    return r;
}

static uint32_t childOf(const std::vector<uint8_t>& indexRecord)
{
    return be32(indexRecord.data() + indexRecord.size() - 4);
}

bool BTree::fits(const std::vector<Record>& recs) const
{
    size_t used = kDescriptor + 2 * (recs.size() + 1);
    for(const Record& r : recs)
        used += r.size();
    return used <= nodeSize_;
}

Error BTree::writeNode(uint32_t n, int8_t k, uint8_t height, uint32_t fLink, uint32_t bLink,
                       const std::vector<Record>& recs)
{
    std::vector<uint8_t> buf(nodeSize_, 0);
    put32(buf.data(), fLink);
    put32(buf.data() + 4, bLink);
    buf[8] = uint8_t(k);
    buf[9] = height;
    put16(buf.data() + 10, uint16_t(recs.size()));
    size_t off = kDescriptor;
    for(size_t i = 0; i < recs.size(); i++)
    {
        put16(buf.data() + nodeSize_ - 2 * (i + 1), uint16_t(off));
        memcpy(buf.data() + off, recs[i].data(), recs[i].size());
        off += recs[i].size();
    }
    put16(buf.data() + nodeSize_ - 2 * (recs.size() + 1), uint16_t(off));
    if(!file_.write(uint64_t(n) * nodeSize_, buf.data(), nodeSize_))
        return Error::io;
    auto it = index_.find(n);
    if(it != index_.end())
        *it->second->second = buf;
    return Error::none;
}

Error BTree::setLinks(uint32_t n, std::optional<uint32_t> fLink, std::optional<uint32_t> bLink)
{
    Node nd = node(n);
    if(!nd)
        return Error::io;
    std::vector<uint8_t> buf = *nd;
    if(fLink)
        put32(buf.data(), *fLink);
    if(bLink)
        put32(buf.data() + 4, *bLink);
    if(!file_.write(uint64_t(n) * nodeSize_, buf.data(), nodeSize_))
        return Error::io;
    *nd = buf;
    return Error::none;
}

bool BTree::path(const std::vector<uint8_t>& key, std::vector<Step> *steps)
{
    steps->clear();
    uint32_t n = root_;
    for(int guard = 0; n && guard < 32; guard++)
    {
        Node nd = node(n);
        if(!nd)
            return false;
        steps->push_back({ n, kind(nd), (*nd)[9] });
        if(kind(nd) == kLeafNode)
            return true;
        if(kind(nd) != kIndexNode)
            return false;
        uint32_t child = 0;
        const uint8_t *k, *data;
        size_t room;
        for(uint16_t i = 0; i < count(nd); i++)
        {
            if(!record(nd, i, &k, &data, &room) || room < 4)
                return false;
            if(i > 0 && order_(key.data(), k) < 0)
                break;
            child = be32(data);
        }
        n = child;
    }
    return false;
}

/* recs into the node at steps[level]: split in two if they don't fit (the
   new right half's index record added to the parent, or a new root made),
   and the parent's index key for the node kept its first key. */
Error BTree::store(std::vector<Step>& steps, size_t level, std::vector<Record> recs)
{
    const Step s = steps[level];
    Node old = node(s.node);
    if(!old)
        return Error::io;
    uint32_t fl = next(old), bl = be32(old->data() + 4);
    std::vector<Record> before = records(old);
    Error e;

    if(fits(recs))
    {
        if((e = writeNode(s.node, s.kind, s.height, fl, bl, recs)) != Error::none)
            return e;
        if(level == 0 || recs.empty())
            return Error::none;
        bool same = !before.empty()
            && indexKey(before[0].data()) == indexKey(recs[0].data());
        if(same)
            return Error::none;
        Node parent = node(steps[level - 1].node);
        std::vector<Record> precs = records(parent);
        for(Record& r : precs)
            if(childOf(r) == s.node)
            {
                Record updated = indexRecord(recs[0], s.node);
                if(updated == r)
                    return Error::none;
                r = updated;
                return store(steps, level - 1, precs);
            }
        return Error::io;
    }

    /* split: about half the bytes each side */
    size_t total = 0, acc = 0, k = 0;
    for(const Record& r : recs)
        total += r.size();
    while(k < recs.size() - 1 && acc + recs[k].size() <= total / 2)
        acc += recs[k++].size();
    if(k == 0)
        k = 1;
    std::vector<Record> left(recs.begin(), recs.begin() + k), right(recs.begin() + k, recs.end());
    if(!fits(left) || !fits(right))
        return Error::io;                       /* a record bigger than half a node: not in HFS+ */
    uint32_t m;
    if((e = allocNode(&m)) != Error::none)
        return e;
    if((e = writeNode(s.node, s.kind, s.height, m, bl, left)) != Error::none
       || (e = writeNode(m, s.kind, s.height, fl, s.node, right)) != Error::none)
        return e;
    if(fl && (e = setLinks(fl, std::nullopt, m)) != Error::none)
        return e;
    if(s.kind == kLeafNode && lastLeaf_ == s.node)
        lastLeaf_ = m;

    if(level == 0)
    {
        uint32_t r;
        if((e = allocNode(&r)) != Error::none)
            return e;
        std::vector<Record> top = { indexRecord(left[0], s.node), indexRecord(right[0], m) };
        if((e = writeNode(r, kIndexNode, uint8_t(s.height + 1), 0, 0, top)) != Error::none)
            return e;
        root_ = r;
        depth_++;
        return Error::none;
    }
    Node parent = node(steps[level - 1].node);
    std::vector<Record> precs = records(parent);
    for(size_t i = 0; i < precs.size(); i++)
        if(childOf(precs[i]) == s.node)
        {
            precs[i] = indexRecord(left[0], s.node);
            precs.insert(precs.begin() + i + 1, indexRecord(right[0], m));
            return store(steps, level - 1, precs);
        }
    return Error::io;
}

/* The node at steps[level], emptied: unlinked, freed, and its index record
   taken out of the parent. */
Error BTree::dropNode(std::vector<Step>& steps, size_t level)
{
    const Step s = steps[level];
    Node old = node(s.node);
    if(!old)
        return Error::io;
    uint32_t fl = next(old), bl = be32(old->data() + 4);
    Error e;
    if(bl && (e = setLinks(bl, fl, std::nullopt)) != Error::none)
        return e;
    if(fl && (e = setLinks(fl, std::nullopt, bl)) != Error::none)
        return e;
    if(s.kind == kLeafNode)
    {
        if(firstLeaf_ == s.node)
            firstLeaf_ = fl;
        if(lastLeaf_ == s.node)
            lastLeaf_ = bl;
    }
    if((e = freeNode(s.node)) != Error::none)
        return e;
    if(level == 0)
    {
        root_ = 0;
        depth_ = 0;
        firstLeaf_ = lastLeaf_ = 0;
        return Error::none;
    }
    Node parent = node(steps[level - 1].node);
    std::vector<Record> precs = records(parent);
    for(size_t i = 0; i < precs.size(); i++)
        if(childOf(precs[i]) == s.node)
        {
            precs.erase(precs.begin() + i);
            return precs.empty() ? dropNode(steps, level - 1) : store(steps, level - 1, precs);
        }
    return Error::io;
}

/* An index root with one child gives way to it. */
Error BTree::collapseRoot()
{
    while(depth_ > 1)
    {
        Node r = node(root_);
        if(!r || kind(r) != kIndexNode || count(r) != 1)
            break;
        std::vector<Record> recs = records(r);
        uint32_t child = childOf(recs[0]);
        Error e = freeNode(root_);
        if(e != Error::none)
            return e;
        root_ = child;
        depth_--;
    }
    return Error::none;
}

Error BTree::insert(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data)
{
    Record rec = key;
    if(rec.size() & 1)
        rec.push_back(0);
    rec.insert(rec.end(), data.begin(), data.end());
    Error e;
    if(!root_)
    {
        uint32_t n;
        if((e = allocNode(&n)) != Error::none || (e = writeNode(n, kLeafNode, 1, 0, 0, { rec })) != Error::none)
            return e;
        root_ = firstLeaf_ = lastLeaf_ = n;
        depth_ = 1;
        leafRecords_ = 1;
        return writeHeader();
    }
    std::vector<Step> steps;
    if(!path(key, &steps))
        return Error::io;
    std::vector<Record> recs = records(node(steps.back().node));
    size_t i = 0;
    for(; i < recs.size(); i++)
    {
        int c = order_(key.data(), recs[i].data());
        if(c == 0)
            return Error::exists;
        if(c < 0)
            break;
    }
    recs.insert(recs.begin() + i, rec);
    if((e = store(steps, steps.size() - 1, recs)) != Error::none)
        return e;
    leafRecords_++;
    return writeHeader();
}

Error BTree::remove(const std::vector<uint8_t>& key)
{
    std::vector<Step> steps;
    if(!root_ || !path(key, &steps))
        return Error::notFound;
    std::vector<Record> recs = records(node(steps.back().node));
    for(size_t i = 0; i < recs.size(); i++)
        if(order_(key.data(), recs[i].data()) == 0)
        {
            recs.erase(recs.begin() + i);
            Error e = recs.empty() ? dropNode(steps, steps.size() - 1) : store(steps, steps.size() - 1, recs);
            if(e == Error::none)
                e = collapseRoot();
            if(e != Error::none)
                return e;
            leafRecords_--;
            return writeHeader();
        }
    return Error::notFound;
}

Error BTree::replace(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data)
{
    std::vector<Step> steps;
    if(!root_ || !path(key, &steps))
        return Error::notFound;
    std::vector<Record> recs = records(node(steps.back().node));
    for(size_t i = 0; i < recs.size(); i++)
        if(order_(key.data(), recs[i].data()) == 0)
        {
            Record rec(recs[i].begin(), recs[i].begin() + keyBytes(recs[i].data(), false));
            rec.insert(rec.end(), data.begin(), data.end());
            recs[i] = rec;
            Error e = store(steps, steps.size() - 1, recs);
            return e != Error::none ? e : writeHeader();
        }
    return Error::notFound;
}

/* ---- the node map and the header ---------------------------------------- */

bool BTree::mapBit(uint32_t n, uint64_t *fileByte, uint8_t *mask)
{
    /* the header node's map record (record 2), then map nodes' record 0 */
    uint32_t nodeNum = 0;
    uint32_t covered = 0;
    for(int guard = 0; guard < 4096; guard++)
    {
        Node nd = node(nodeNum);
        if(!nd)
            return false;
        int rec = kind(nd) == kHeaderNode ? 2 : 0;
        const uint8_t *base = nd->data();
        if(count(nd) <= rec)
            return false;
        uint16_t a = be16(base + nodeSize_ - 2 * (rec + 1)), b = be16(base + nodeSize_ - 2 * (rec + 2));
        uint32_t bits = uint32_t(b - a) * 8;
        if(n < covered + bits)
        {
            uint32_t bit = n - covered;
            *fileByte = uint64_t(nodeNum) * nodeSize_ + a + bit / 8;
            *mask = uint8_t(0x80 >> (bit % 8));
            return true;
        }
        covered += bits;
        nodeNum = next(nd);
        if(!nodeNum)
            return false;
    }
    return false;
}

Error BTree::allocNode(uint32_t *out)
{
    for(int attempt = 0; attempt < 2; attempt++)
    {
        if(freeNodes_)
            for(uint32_t n = 1; n < totalNodes_; n++)
            {
                uint64_t at;
                uint8_t mask, byte;
                if(!mapBit(n, &at, &mask) || file_.read(at, &byte, 1) != 1)
                    return Error::io;
                if(byte & mask)
                    continue;
                byte |= mask;
                if(!file_.write(at, &byte, 1))
                    return Error::io;
                auto it = index_.find(uint32_t(at / nodeSize_));
                if(it != index_.end())
                    (*it->second->second)[at % nodeSize_] = byte;
                freeNodes_--;
                *out = n;
                return writeHeader();
            }
        /* none free: the file grows (at least a clump, at least 4 nodes) */
        uint32_t want = std::max<uint32_t>(4, clumpSize_ / nodeSize_);
        Error e = volume_.growTree(*this, want);
        if(e != Error::none)
            return e;
    }
    return Error::diskFull;
}

Error BTree::freeNode(uint32_t n)
{
    uint64_t at;
    uint8_t mask, byte;
    if(!mapBit(n, &at, &mask) || file_.read(at, &byte, 1) != 1)
        return Error::io;
    byte &= uint8_t(~mask);
    if(!file_.write(at, &byte, 1))
        return Error::io;
    auto it = index_.find(uint32_t(at / nodeSize_));
    if(it != index_.end())
        (*it->second->second)[at % nodeSize_] = byte;
    std::vector<uint8_t> zero(nodeSize_, 0);
    if(!file_.write(uint64_t(n) * nodeSize_, zero.data(), nodeSize_))
        return Error::io;
    auto old = index_.find(n);
    if(old != index_.end())
    {
        lru_.erase(old->second);
        index_.erase(old);
    }
    freeNodes_++;
    return Error::none;
}

Error BTree::added(uint32_t nodes)
{
    uint64_t at;
    uint8_t mask;
    if(!mapBit(totalNodes_ + nodes - 1, &at, &mask))
        return Error::diskFull;                 /* past what the map covers: no map nodes added */
    totalNodes_ += nodes;
    freeNodes_ += nodes;
    return writeHeader();
}

Error BTree::writeHeader()
{
    Node h = node(0);
    if(!h)
        return Error::io;
    std::vector<uint8_t> buf = *h;
    uint8_t *r = buf.data() + kDescriptor;
    put16(r, depth_);
    put32(r + 2, root_);
    put32(r + 6, leafRecords_);
    put32(r + 10, firstLeaf_);
    put32(r + 14, lastLeaf_);
    put32(r + 22, totalNodes_);
    put32(r + 26, freeNodes_);
    if(!file_.write(0, buf.data(), nodeSize_))
        return Error::io;
    *h = buf;
    return Error::none;
}
