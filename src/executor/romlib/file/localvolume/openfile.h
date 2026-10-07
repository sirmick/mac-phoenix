#pragma once

#include <stdlib.h>
#include <FileMgr.h>

namespace Executor
{
class UpgradeRequiredException : std::runtime_error
{
public:
    UpgradeRequiredException() : std::runtime_error("file metadata representation update required") {}
};

class OpenFile
{
public:
    virtual ~OpenFile() = default;

    virtual size_t getEOF() = 0;
    virtual void setEOF(size_t sz) { throw UpgradeRequiredException(); }
    virtual size_t read(size_t offset, void *p, size_t n) = 0;
    virtual size_t write(size_t offset, void *p, size_t n) { throw UpgradeRequiredException(); }

    virtual bool canWrite() const { return true; }
};

class EmptyFork : public OpenFile
{
public:
    virtual size_t getEOF() override { return 0; }
    virtual size_t read(size_t offset, void *p, size_t n) override { return 0; }
};
}
