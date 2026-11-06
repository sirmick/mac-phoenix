#pragma once

#include "cnidmapper.h"
#include <map>
#include <unordered_map>

namespace Executor
{

class SimpleCNIDMapper : public CNIDMapper
{
    CNID nextCNID_ = 3;

    struct StoredMapping
    {
        CNID parID;
        CNID cnid;
        fs::path path;
        mac_string macname;
    };
    std::unordered_map<CNID, StoredMapping> mappings_;
    std::unordered_map<CNID, std::vector<CNID>> directories_; 
public:
    explicit SimpleCNIDMapper(fs::path root, mac_string volumeName);

    virtual std::vector<Mapping> mapDirectoryContents(CNID dirID, std::vector<fs::directory_entry> realPaths) override;
    virtual std::optional<Mapping> lookupCNID(CNID cnid) override;
    virtual void deleteCNID(CNID cnid) override;
    virtual void moveCNID(CNID cnid, CNID newParent, mac_string_view newMacName, std::function<fs::path()> fsop) override;
};

}
