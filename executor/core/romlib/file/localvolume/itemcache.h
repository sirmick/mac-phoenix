#pragma once

#include <rsys/filesystem.h>
#include <util/macstrings.h>
#include <memory>
#include <chrono>
#include <list>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <stdint.h>

namespace Executor
{
class Item;
class ItemFactory;
class CNIDMapper;
class DirectoryItem;

using ItemPtr = std::shared_ptr<Item>;
using DirectoryItemPtr = std::shared_ptr<DirectoryItem>;

using CNID = int32_t;

class ItemCache
{
    std::unique_ptr<CNIDMapper> cnidMapper_;
    ItemFactory *itemFactory_;

    std::unordered_map<CNID, std::weak_ptr<Item>> items_; 
    
    DirectoryItemPtr rootDirItem_;

    struct CachedDirectory
    {
        DirectoryItemPtr directory;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::list<CachedDirectory> cachedDirectories_;

    /* MacPhoenix: the names cacheDirectory() just listed in one directory
       (valid only during that call's createItemForDirEntry loop): a
       factory's "does a same-folder sidecar exist" check (AppleDouble's
       %name/._name) is a set lookup instead of a fresh stat per file, the
       File Manager's biggest cost on Emscripten's JS MEMFS. */
    fs::path scanDir_;
    std::set<std::string> scanNames_;

    void cleanDirectoryCache();
public:
    ItemCache(fs::path root, mac_string_view volumeName,
                std::unique_ptr<CNIDMapper> cnidMapper,
                ItemFactory *itemFactory
                );
    ~ItemCache();

    void noteItemFreed(CNID cnid);

    ItemPtr tryResolve(CNID cnid);
    ItemPtr tryResolve(fs::path path);


    void cacheDirectory(DirectoryItemPtr item);
    /* Whether `filename` is a (non-hidden) entry of `dir`, if `dir` is the
       directory cacheDirectory() is currently scanning; nullopt (not "no
       such file") when the scan info isn't there or is for another
       directory, so callers fall back to a real stat themselves. */
    std::optional<bool> siblingExistsInScan(const fs::path& dir, const std::string& filename) const;
    ItemPtr lookup(DirectoryItemPtr dir, mac_string_view name);
    void flushDirectoryCache(DirectoryItemPtr item);
    void flushDirectoryCache(CNID dirID);
    void flushItem(ItemPtr item);

    void deleteItem(ItemPtr item);
    void renameItem(ItemPtr item, mac_string_view newName);
    void moveItem(ItemPtr item, DirectoryItemPtr newParent);
    void rebaseDescendants(const fs::path& oldPath, const fs::path& newPath);
};

}
