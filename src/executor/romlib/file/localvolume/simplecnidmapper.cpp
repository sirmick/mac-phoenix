#include "simplecnidmapper.h"
#include <OSUtil.h>

#include <algorithm>
#include <set>

using namespace Executor;

SimpleCNIDMapper::SimpleCNIDMapper(fs::path root, mac_string volumeName)
{
    mappings_[2] = {
        1, 2,
        root,
        volumeName
    };
}

std::vector<CNIDMapper::Mapping> SimpleCNIDMapper::mapDirectoryContents(CNID dirID,
        std::vector<fs::directory_entry> realPaths)
{
    auto& cachedContents = directories_[dirID];
    
    std::sort(realPaths.begin(), realPaths.end());

    std::vector<Mapping> dirMappings;
    dirMappings.reserve(realPaths.size());

    std::vector<CNID> newContents;
    newContents.reserve(realPaths.size());

    auto cacheIt = cachedContents.begin(), cacheEnd = cachedContents.end();
    StoredMapping* mapping = cacheIt != cacheEnd ? &mappings_.at(*cacheIt) : nullptr;
    
    std::set<mac_string> usedNames;
    // FIXME: add names that are already in use by existing mappings

    for(auto& entry : realPaths)
    {
        while(mapping && mapping->path < entry.path())
        {
            // discard mapping
            deleteCNID(mapping->cnid);
            ++cacheIt;
            mapping = cacheIt != cacheEnd ? &mappings_.at(*cacheIt) : nullptr;
        }

        if(mapping && mapping->path == entry.path())
        {
            // use existing mapping
            dirMappings.push_back({mapping->parID, mapping->cnid, std::move(entry), mapping->macname});
            newContents.push_back(mapping->cnid);

            ++cacheIt;
            mapping = cacheIt != cacheEnd ? &mappings_.at(*cacheIt) : nullptr;
        }
        else
        {
            // new mapping
            mac_string macname;
            const fs::path& name = entry.path().filename();
            int index = 0;

            bool nameIsFree;
            do
            {
                macname = toMacRomanFilename(name, index++);
                mac_string nameUpr = macname;
                ROMlib_UprString(nameUpr.data(), false, nameUpr.size());
                nameIsFree = usedNames.emplace(std::move(nameUpr)).second;
            } while(!nameIsFree);

            Mapping newMapping {
                dirID, nextCNID_++,
                std::move(entry),
                std::move(macname)
            };
            dirMappings.push_back(newMapping);
            newContents.push_back(newMapping.cnid);
            mappings_.emplace(newMapping.cnid, StoredMapping{
                newMapping.parID, newMapping.cnid,
                newMapping.entry.path(),
                std::move(newMapping.macname)
            });
        }
    }

    cachedContents = std::move(newContents);

    return dirMappings;
}

std::optional<CNIDMapper::Mapping> SimpleCNIDMapper::lookupCNID(CNID cnid)
{
    auto it = mappings_.find(cnid);
    if(it == mappings_.end())
        return {};

    if(fs::exists(it->second.path))
    {
        try
        {
            return Mapping {
                it->second.parID,
                it->second.cnid,
                fs::directory_entry(it->second.path),
                it->second.macname
            };
        }
        catch(const fs::filesystem_error&)
        {
            // file no longer exists,
            // pass through
        }
    }
        
    deleteCNID(cnid);
    return {};
}


void SimpleCNIDMapper::deleteCNID(CNID cnid)
{
    auto it = mappings_.find(cnid);
    if(it == mappings_.end())
        return;
    CNID parID = it->second.parID;

    auto& cachedContents = directories_[parID];

    cachedContents.erase(
        std::remove(cachedContents.begin(), cachedContents.end(), cnid),
        cachedContents.end());

    mappings_.erase(it);

    auto dirIt = directories_.find(cnid);
    if(dirIt != directories_.end())
    {
        // deleteCNID is only invoked for empty directories by ItemCacher,
        // but mapDirectoryContents and lookupCNID above also invoke it
        // on files and directories that have been deleted or moved by other programs.
        for(CNID child : dirIt->second)
            deleteCNID(child);
        directories_.erase(cnid);
    }
}

void SimpleCNIDMapper::moveCNID(CNID cnid, CNID newParent, mac_string_view newMacName, std::function<fs::path()> fsop)
{
    auto it = mappings_.find(cnid);
    if(it == mappings_.end())
        return;

    CNID oldParent = it->second.parID;
    if(newParent == 0)
        newParent = oldParent;
    if(oldParent == newParent && newMacName.empty())
        return;

    fs::path newPath = fsop();

    it->second.path = newPath;
    it->second.parID = newParent;
    if(!newMacName.empty())
        it->second.macname = newMacName;

    auto& cachedContentsFrom = directories_[oldParent];
    cachedContentsFrom.erase(
        std::remove(cachedContentsFrom.begin(), cachedContentsFrom.end(), cnid),
        cachedContentsFrom.end());

    auto& cachedContentsTo = directories_[newParent];
    cachedContentsTo.insert(
        std::upper_bound(cachedContentsTo.begin(), cachedContentsTo.end(), cnid,
            [&](CNID a, CNID b) {
                return mappings_.at(a).path < mappings_.at(b).path;
            }),
        cnid);
}
