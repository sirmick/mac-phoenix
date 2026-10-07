#include "gtest/gtest.h"

#include <file/file.h>
#include <util/macstrings.h>

using namespace Executor;

TEST(FilesInternal, pathToFSSpec)
{
    auto path = fs::current_path();

    std::string macpath;
    if(path.root_name().empty())
        macpath = "vol" + path.string();
    else
    {
        macpath = path.root_name().string();
        if(macpath.back() == ':')
            macpath.pop_back();
        macpath += "/" + path.relative_path().string();
    }

    for(char& c : macpath)
    {
        if(c == ':')
            c = '/';
        else if(c == '/')
            c = ':';
#ifdef _WIN32
        else if(c == '\\')
            c = ':';
#endif
    }
    std::cout << macpath;

    auto native = nativePathToFSSpec(path);
    auto mac = cmdlinePathToFSSpec(macpath);

    ASSERT_EQ(true, native.has_value());
    ASSERT_EQ(true, mac.has_value());

    EXPECT_EQ(mac->vRefNum, native->vRefNum);
    EXPECT_EQ(mac->parID, native->parID);
    EXPECT_EQ(toUnicode(mac->name), toUnicode(native->name));
}
