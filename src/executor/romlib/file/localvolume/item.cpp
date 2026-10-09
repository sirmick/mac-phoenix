#include "item.h"
#include "itemcache.h"

#include <OSUtil.h>
#include <time.h>
#include <iostream>

using namespace Executor;

Item::Item(ItemCache& itemcache, CNID parID, CNID cnid, fs::path p, mac_string_view name)
    : itemcache_(itemcache), parID_(parID), cnid_(cnid), path_(std::move(p)), name_(name)
{
}

Item::~Item()
{
    itemcache_.noteItemFreed(cnid_);
}

void Item::deleteItem()
{
    fs::remove(path());
}

void Item::moveItem(const fs::path& newPath, mac_string_view newName)
{
    fs::rename(path(), newPath);
    path_ = newPath;
    if(!newName.empty())
        name_ = newName;
}

/* MacPhoenix: 1904 is a leap year, so 1904-1968 holds 17 leap days, not
   (1970-1904)/4 = 16; the old value made every host file's date a day early
   (and Finder 7.5 rebuilt its segment cache, keyed on its own file date). */
const int64_t macToUnixEpoch = 86400 * (365 * (1970-1904) + (1970-1904+3)/4);

/* MacPhoenix: Mac dates are local wall-clock time, host file times UTC.
   Convert each date with the host's offset at that date (as `ls` shows
   it), or a date the Finder sets on a folder reads back hours off and its
   cached views of the folder go stale. Per-date rather than today's offset,
   so a file's Mac date doesn't move when daylight saving starts or ends
   (Finder rebuilds its segment cache when its own file's date changes).
   tools/macdecode/sideload_system.py sets host times the same way. */
int64_t Executor::hostToMacTime(int64_t t)
{
    time_t tt = t;
    struct tm tm;
    localtime_r(&tt, &tm);
    return t + tm.tm_gmtoff + macToUnixEpoch;
}

int64_t Executor::macToHostTime(int64_t t)
{
    time_t wall = t - macToUnixEpoch;
    struct tm tm;
    gmtime_r(&wall, &tm);
    tm.tm_isdst = -1;
    return mktime(&tm);
}

ItemInfo Item::getInfo()
{
    ItemInfo info{};
    info.modTime = hostToMacTime(fs::last_write_time(path()));

    return info;
}

void Item::setInfo(ItemInfo info)
{
    auto oldModTime = hostToMacTime(fs::last_write_time(path()));
    if(oldModTime != (int64_t)info.modTime)
        fs::last_write_time(path(), macToHostTime(info.modTime));
}

ItemPtr DirectoryItemFactory::createItemForDirEntry(ItemCache& itemcache, CNID parID, CNID cnid,
    const fs::directory_entry& e, mac_string_view macname)
{
    if(fs::is_directory(e.path()))
    {
        return std::make_shared<DirectoryItem>(itemcache, parID, cnid, e.path(), macname);
    }
    return nullptr;
}

void DirectoryItem::clearCache()
{
    cache_valid_ = false;
    contents_.clear();
    contents_by_name_.clear();
    files_.clear();
}

void DirectoryItem::populateCache(std::vector<ItemPtr> items)
{
    if(cache_valid_)
        return;

    contents_ = std::move(items);

    for(const ItemPtr& item : contents_)
    {
        mac_string nameUpr = item->name();
        ROMlib_UprString(nameUpr.data(), false, nameUpr.size());

        assert(nameUpr.size());
        [[maybe_unused]] auto inserted = contents_by_name_.emplace(nameUpr, item).second;
        assert(inserted);

        if(!dynamic_cast<DirectoryItem*>(item.get()))
            files_.push_back(item);
    }

    cache_valid_ = true;
}

ItemPtr DirectoryItem::tryResolve(mac_string_view name)
{
    assert(cache_valid_);
    mac_string nameUpr { name };
    ROMlib_UprString(nameUpr.data(), false, nameUpr.size());
    auto it = contents_by_name_.find(nameUpr);
    if(it != contents_by_name_.end())
        return it->second;
    return {};
}

ItemPtr DirectoryItem::tryResolve(fs::path name)
{
    assert(cache_valid_);
    fs::path path = path_ / name;
    for(auto& item : contents_)
        if(item->path() == path)
            return item;
    return {};
}

ItemPtr DirectoryItem::resolve(int index, bool includeDirectories)
{
    assert(cache_valid_);
    const auto& array = includeDirectories ? contents_ : files_;
    if(index >= 1 && index <= array.size())
        return array[index-1];
    throw OSErrorException(fnfErr);
}

/* MacPhoenix: a folder is empty when it holds nothing but the sidecars of
   items that are gone (.finf/.rsrc entries are left by a folder moved or
   deleted out of it); those go with it, as does its own Finder info in
   its parent's .finf. Was: busy whenever .finf held anything. */
void DirectoryItem::deleteItem()
{
    boost::system::error_code ec;
    for(const auto& e : fs::directory_iterator(path(), ec))
    {
        auto name = e.path().filename().string();
        if(name != ".finf" && name != ".rsrc")
            throw OSErrorException(fBsyErr);
    }
    fs::path own = finderInfoPath();
    fs::remove_all(path() / ".rsrc", ec);
    fs::remove_all(path() / ".finf", ec);

    fs::remove(path(), ec);

    if(ec)
    {
        if(ec == boost::system::errc::directory_not_empty)
            throw OSErrorException(fBsyErr);
        else
            throw OSErrorException(paramErr);
    }
    fs::remove(own, ec);
}

/* MacPhoenix: the folder's Finder info moves with it (from the old
   parent's .finf to the new one's), keeping both parents' dates. */
void DirectoryItem::moveItem(const fs::path& newPath, mac_string_view newName)
{
    fs::path oldInfo = finderInfoPath();
    Item::moveItem(newPath, newName);
    fs::path newInfo = finderInfoPath();
    boost::system::error_code ec;
    if(oldInfo == newInfo || !fs::exists(oldInfo, ec))
        return;
    fs::path oldHolder = oldInfo.parent_path().parent_path();
    fs::path newHolder = newInfo.parent_path().parent_path();
    auto oldTime = fs::last_write_time(oldHolder, ec);
    auto newTime = fs::last_write_time(newHolder, ec);
    fs::create_directory(newInfo.parent_path(), ec);
    fs::rename(oldInfo, newInfo, ec);
    fs::last_write_time(oldHolder, oldTime, ec);
    fs::last_write_time(newHolder, newTime, ec);
}

/* MacPhoenix: a folder's Finder info (DInfo + DXInfo) lives where Basilisk
   II's ExtFS keeps it, in the parent's .finf/<name>; the volume's root
   keeps its own in .finf/.root. Finder sets it on every folder (window
   position, view, flags), and failing that made it retry endlessly. */
fs::path DirectoryItem::finderInfoPath() const
{
    if(cnid() == 2)
        return path() / ".finf" / ".root";
    return path().parent_path() / ".finf" / path().filename();
}

ItemInfo DirectoryItem::getInfo()
{
    ItemInfo info = Item::getInfo();
    info.dir = {};
    fs::ifstream(finderInfoPath(), std::ios::binary).read((char*)&info.dir, sizeof(info.dir));
    return info;
}

void DirectoryItem::setInfo(ItemInfo info)
{
    fs::path finf = finderInfoPath();
    /* .finf sits inside a folder Finder watches: keep that folder's
       modification date, or Finder sees it change and rescans it. */
    fs::path holder = finf.parent_path().parent_path();
    auto holder_time = fs::last_write_time(holder);
    fs::create_directory(finf.parent_path());
    fs::ofstream(finf, std::ios::binary).write((char*)&info.dir, sizeof(info.dir));
    fs::last_write_time(holder, holder_time);
    Item::setInfo(info);
}
