#pragma once

#include <vdriver/vdriver.h>

class HeadlessVideoDriver : public Executor::VideoDriver
{
    using VideoDriver::VideoDriver;

    virtual bool setMode(int width, int height, int bpp,
                                bool grayscale_p) override;

    virtual void runEventLoop() override {}
    virtual void endEventLoop() override {}
protected:
    virtual void requestUpdate() override {}
};