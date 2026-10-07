#pragma once

#include "localvolume.h"
#include "item.h"

namespace Executor
{
class MacFileItem : public FileItem
{
public:
    using FileItem::FileItem;

    virtual ItemInfo getInfo() override;
    virtual void setInfo(ItemInfo info) override;
    virtual std::unique_ptr<OpenFile> open(int8_t permission) override;
    virtual std::unique_ptr<OpenFile> openRF(int8_t permission) override;
};


class MacItemFactory : public ItemFactory
{
    virtual ItemPtr createItemForDirEntry(ItemCache& itemcache, CNID parID, CNID cnid,
        const fs::directory_entry& e, mac_string_view macname) override;
    virtual void createFile(const fs::path& newPath) override;
};
}