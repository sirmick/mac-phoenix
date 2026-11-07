#pragma once

#include "localvolume.h"
#include "item.h"

namespace Executor
{
class BasiliskItemFactory : public ItemFactory
{
public:
    virtual bool isHidden(const fs::directory_entry &e) override;
    virtual ItemPtr createItemForDirEntry(ItemCache& itemcache, CNID parID, CNID cnid,
        const fs::directory_entry& e, mac_string_view macname) override;
    virtual void createFile(const fs::path& newPath) override;
};

class BasiliskFileItem : public FileItem
{
public:
    using FileItem::FileItem;

    virtual ItemInfo getInfo() override;
    virtual void setInfo(ItemInfo info) override;
    virtual std::unique_ptr<OpenFile> open(int8_t permission) override;
    virtual std::unique_ptr<OpenFile> openRF(int8_t permission) override;

    virtual void deleteItem() override;
    virtual void moveItem(const fs::path& newPath, mac_string_view newName) override;
};
} // namespace Executor