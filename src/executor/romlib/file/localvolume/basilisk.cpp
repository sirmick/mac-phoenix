#include "basilisk.h"
#include "plain.h"

using namespace Executor;

bool BasiliskItemFactory::isHidden(const fs::directory_entry& e)
{
    if(e.path().filename().string() == ".rsrc")
        return true;
    else if(e.path().filename().string() == ".finf")
        return true;
    return false;
}


ItemPtr BasiliskItemFactory::createItemForDirEntry(ItemCache& itemcache, CNID parID, CNID cnid,
    const fs::directory_entry& e, mac_string_view macname)
{
    if(fs::is_regular_file(e.path()))
    {
        fs::path rsrc = e.path().parent_path() / ".rsrc" / e.path().filename();
        fs::path finf = e.path().parent_path() / ".finf" / e.path().filename();
        
        if(fs::is_regular_file(rsrc) || fs::is_regular_file(finf))
            return std::make_shared<BasiliskFileItem>(itemcache, parID, cnid, e.path(), macname);
    }
    return nullptr;
}

void BasiliskItemFactory::createFile(const fs::path& newPath)
{
    fs::path parentPath = newPath.parent_path();
    fs::path fn = newPath.filename();

    fs::ofstream(parentPath / fn);

    fs::create_directory(parentPath / ".rsrc");
    fs::create_directory(parentPath / ".finf");

    fs::ofstream(parentPath / ".rsrc" / fn, std::ios::binary);
    FInfo info = {};
    fs::ofstream(parentPath / ".finf" / fn, std::ios::binary).write((char*)&info, sizeof(info));
}

std::unique_ptr<OpenFile> BasiliskFileItem::open(int8_t permission)
{
    return std::make_unique<PlainDataFork>(path_, permission);
}
std::unique_ptr<OpenFile> BasiliskFileItem::openRF(int8_t permission)
{
    fs::path rsrc = path().parent_path() / ".rsrc" / path().filename();
    return std::make_unique<PlainDataFork>(rsrc, permission);
}

ItemInfo BasiliskFileItem::getInfo()
{
    fs::path finf = path().parent_path() / ".finf" / path().filename();

    ItemInfo info = FileItem::getInfo();
    fs::ifstream(finf, std::ios::binary).read((char*)&info, sizeof(info.file));

    return info;
}

void BasiliskFileItem::setInfo(ItemInfo info)
{
    fs::path finf = path().parent_path() / ".finf" / path().filename();

    FileItem::setInfo(info);
    fs::ofstream(finf, std::ios::binary).write((char*)&info, sizeof(info.file));
}

void BasiliskFileItem::deleteItem()
{
    fs::remove(path());
    fs::path rsrc = path().parent_path() / ".rsrc" / path().filename();
    fs::path finf = path().parent_path() / ".finf" / path().filename();
    boost::system::error_code ec;
    fs::remove(rsrc, ec);
    fs::remove(finf, ec);
}

void BasiliskFileItem::moveItem(const fs::path& newPath, mac_string_view newName)
{
    fs::path rsrcOld = path().parent_path() / ".rsrc" / path().filename();
    fs::path finfOld = path().parent_path() / ".finf" / path().filename();
    fs::path rsrcNew = newPath.parent_path() / ".rsrc" / newPath.filename();
    fs::path finfNew = newPath.parent_path() / ".finf" / newPath.filename();

    fs::rename(path(), newPath);

    boost::system::error_code ec;
    fs::rename(rsrcOld, rsrcNew, ec);
    fs::rename(finfOld, finfNew, ec);

    path_ = std::move(newPath);
    if(!newName.empty())
        name_ = newName;
}
