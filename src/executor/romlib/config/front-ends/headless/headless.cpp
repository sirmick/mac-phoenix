#include "headless.h"

bool HeadlessVideoDriver::setMode(int width, int height, int bpp,
                                  bool grayscale_p)
{
    if(width == 0 || height == 0)
    {
        width = 512;
        height = 342;
    }
    if(bpp == 0)
        bpp = 1;

    framebuffer_ = Executor::Framebuffer(width, height, bpp);
    return true;
}
