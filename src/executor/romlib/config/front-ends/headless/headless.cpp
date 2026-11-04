#include "headless.h"

bool HeadlessVideoDriver::setMode(int width, int height, int bpp,
                                  bool grayscale_p)
{
    if(width == 0 || height == 0) /* the size it has (SetDepth), 512x342 the first time */
    {
        width = framebuffer_.width ? framebuffer_.width : 512;
        height = framebuffer_.height ? framebuffer_.height : 342;
    }
    if(bpp == 0)
        bpp = 1;

    framebuffer_ = Executor::Framebuffer(width, height, bpp);
    return true;
}
