#pragma once
/* btree.h -- an HFS+ B-tree file (TN1150 "B-Trees"): the catalog's or the
 * extents overflow file's. Reading: a cache of nodes, keyed search, leaf
 * walks. Writing (on a writable volume): records inserted, removed and
 * replaced; full nodes split, empty ones freed, index keys kept the first
 * keys of their children, nodes allocated through the tree's map (the
 * file grown through the volume when none is free). Every changed node is
 * written at once. */
#include "reader.h"

namespace Executor::hfsplus
{

class BTree
{
public:
    using Node = std::shared_ptr<std::vector<uint8_t>>;
    /* <0, 0, >0: the key sought against a node's key */
    using Compare = std::function<int(const uint8_t *key)>;
    /* the order of two keys */
    using Order = std::function<int(const uint8_t *a, const uint8_t *b)>;

    struct Position
    {
        uint32_t node = 0;
        uint16_t record = 0;
    };

    BTree(Reader& volume, Fork& file, uint32_t fileID, Order order)
        : volume_(volume), file_(file), fileID_(fileID), order_(std::move(order)) {}

    bool open();
    uint8_t keyCompareType() const { return keyCompareType_; }
    uint16_t nodeSize() const { return nodeSize_; }
    uint32_t fileID() const { return fileID_; }
    Fork& file() { return file_; }

    Node node(uint32_t n);
    static int8_t kind(const Node& n) { return int8_t((*n)[8]); }
    static uint16_t count(const Node& n) { return be16(n->data() + 10); }
    static uint32_t next(const Node& n) { return be32(n->data()); }

    /* A record's key and data, and the bytes from the data to the node's end. */
    bool record(const Node& n, uint16_t i, const uint8_t **key, const uint8_t **data, size_t *room);
    /* The first leaf record whose key is >= the one sought. */
    bool seek(const Compare& cmp, Position *pos);
    /* The record at pos (moved on to the next leaf at a leaf's end). */
    bool at(Position *pos, const uint8_t **key, const uint8_t **data, size_t *room, Node *keep);

    /* key in its on-disk form, keyLength first; data, the record's data */
    Error insert(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
    Error remove(const std::vector<uint8_t>& key);
    Error replace(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);

    /* The tree's file grew by `nodes` nodes (Reader::growTree). */
    Error added(uint32_t nodes);

private:
    using Record = std::vector<uint8_t>;
    struct Step
    {
        uint32_t node;
        int8_t kind;
        uint8_t height;
    };

    std::vector<Record> records(const Node& n);
    size_t keyBytes(const uint8_t *key, bool index) const;
    std::vector<uint8_t> indexKey(const uint8_t *key) const;
    Record indexRecord(const Record& first, uint32_t child) const;
    bool fits(const std::vector<Record>& recs) const;
    Error writeNode(uint32_t n, int8_t kind, uint8_t height, uint32_t fLink, uint32_t bLink,
                    const std::vector<Record>& recs);
    Error setLinks(uint32_t n, std::optional<uint32_t> fLink, std::optional<uint32_t> bLink);
    /* root to leaf, for the key */
    bool path(const std::vector<uint8_t>& key, std::vector<Step> *steps);
    Error store(std::vector<Step>& steps, size_t level, std::vector<Record> recs);
    Error dropNode(std::vector<Step>& steps, size_t level);
    Error collapseRoot();
    Error allocNode(uint32_t *n);
    Error freeNode(uint32_t n);
    Error writeHeader();
    /* the map: the header node's map record, then each map node's */
    struct MapRecord
    {
        uint32_t node;
        uint16_t offset, bytes;
        uint32_t first;                         /* the node its first bit is for */
    };
    bool mapRecords(std::vector<MapRecord> *out);
    /* the map's bit for a node: where it is in the file */
    bool mapBit(uint32_t n, uint64_t *fileByte, uint8_t *mask);
    Error setMapBit(uint32_t n, bool used);

    Reader& volume_;
    Fork& file_;
    uint32_t fileID_;
    Order order_;
    uint16_t depth_ = 0, nodeSize_ = 0, maxKeyLength_ = 0;
    uint32_t root_ = 0, leafRecords_ = 0, firstLeaf_ = 0, lastLeaf_ = 0, totalNodes_ = 0, freeNodes_ = 0;
    uint32_t clumpSize_ = 0, attributes_ = 0;
    uint8_t keyCompareType_ = 0;
    std::list<std::pair<uint32_t, Node>> lru_;
    std::unordered_map<uint32_t, std::list<std::pair<uint32_t, Node>>::iterator> index_;

    friend class Reader;
};

} // namespace Executor::hfsplus
