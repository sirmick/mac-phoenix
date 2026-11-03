/* cursordev.cpp — the Cursor Device Manager (CursorDevices.h; trap $AADB,
 * and CursorDevicesLib for PowerPC code: multiversal/defs/CursorDevices.yaml).
 *
 * Executor has one pointing device, the mouse the host drives: the first
 * record in the list, made at the first call. Programs read and set its
 * fields (9.0.4's Mouse control panel sets the double-click time and the
 * acceleration at startup, Marathon 2 moves the cursor with it); moving it
 * moves the cursor, its buttons post mouse-down and mouse-up events. Records
 * a program adds with CursorDeviceNewDevice join the list and are its own.
 */

#include <base/common.h>
#include <CursorDevices.h>
#include <MemoryMgr.h>
#include <OSEvent.h>
#include <ToolboxEvent.h>
#include <rsys/adb.h>

using namespace Executor;

namespace
{
CursorDevicePtr devices; /* the list; the mouse first */

void ensure_mouse()
{
    if(devices)
        return;
    CursorDevicePtr d = (CursorDevicePtr)NewPtrSysClear(sizeof(CursorDevice));
    if(!d)
        return;
    d->devClass = kDeviceClassMouse;
    d->cntButtons = 1;
    d->resolution = 100 << 16; /* units per inch */
    d->buttonOp[0] = kButtonSingleClick;
    d->doubleClickTime = LM(DoubleTime);
    devices = d;
}

bool known(CursorDevicePtr dev)
{
    ensure_mouse();
    for(CursorDevicePtr d = devices; d; d = d->nextCursorDevice)
        if(d == dev)
            return true;
    return false;
}

void move_to(int32_t h, int32_t v)
{
    LM(MouseLocation2) = LM(MTemp) = LM(MouseLocation) = Point{ (int16_t)v, (int16_t)h };
    adb_apeiron_hack();
}

void button(CursorDevicePtr dev, bool down)
{
    if(down)
        dev->buttons |= 1;
    else
        dev->buttons &= ~1;
    LM(MBState) = down ? 0 : 0xFF;
    PostEvent(down ? mouseDown : mouseUp, 0);
}
}

OSErr Executor::C_CursorDeviceMove(CursorDevicePtr ourDevice, int32_t deltaX, int32_t deltaY)
{
    if(!known(ourDevice))
        return paramErr;
    Point p = LM(MouseLocation);
    move_to(p.h + deltaX, p.v + deltaY);
    return noErr;
}

OSErr Executor::C_CursorDeviceMoveTo(CursorDevicePtr ourDevice, int32_t absX, int32_t absY)
{
    if(!known(ourDevice))
        return paramErr;
    move_to(absX, absY);
    return noErr;
}

OSErr Executor::C_CursorDeviceFlush(CursorDevicePtr ourDevice)
{
    return known(ourDevice) ? noErr : paramErr;
}

OSErr Executor::C_CursorDeviceButtons(CursorDevicePtr ourDevice, short buttons)
{
    if(!known(ourDevice))
        return paramErr;
    if((buttons & 1) != (ourDevice->buttons & 1))
        button(ourDevice, buttons & 1);
    ourDevice->buttons = buttons;
    return noErr;
}

OSErr Executor::C_CursorDeviceButtonDown(CursorDevicePtr ourDevice)
{
    if(!known(ourDevice))
        return paramErr;
    button(ourDevice, true);
    return noErr;
}

OSErr Executor::C_CursorDeviceButtonUp(CursorDevicePtr ourDevice)
{
    if(!known(ourDevice))
        return paramErr;
    button(ourDevice, false);
    return noErr;
}

OSErr Executor::C_CursorDeviceButtonOp(CursorDevicePtr ourDevice, short buttonNumber,
                                       ButtonOpcode opcode, int32_t data)
{
    if(!known(ourDevice) || buttonNumber < 0 || buttonNumber > 7)
        return paramErr;
    ourDevice->buttonOp[buttonNumber] = opcode;
    ourDevice->buttonData[buttonNumber] = data;
    return noErr;
}

OSErr Executor::C_CursorDeviceSetButtons(CursorDevicePtr ourDevice, short numberOfButtons)
{
    if(!known(ourDevice) || numberOfButtons < 0 || numberOfButtons > 8)
        return paramErr;
    ourDevice->cntButtons = numberOfButtons;
    return noErr;
}

OSErr Executor::C_CursorDeviceSetAcceleration(CursorDevicePtr ourDevice, Fixed acceleration)
{
    if(!known(ourDevice))
        return paramErr;
    ourDevice->acceleration = acceleration;
    return noErr;
}

OSErr Executor::C_CursorDeviceDoubleTime(CursorDevicePtr ourDevice, int32_t durationTicks)
{
    if(!known(ourDevice))
        return paramErr;
    ourDevice->doubleClickTime = durationTicks;
    return noErr;
}

OSErr Executor::C_CursorDeviceUnitsPerInch(CursorDevicePtr ourDevice, Fixed resolution)
{
    if(!known(ourDevice))
        return paramErr;
    ourDevice->resolution = resolution;
    return noErr;
}

/* nil: the first device; the last one: nil */
OSErr Executor::C_CursorDeviceNextDevice(GUEST<CursorDevicePtr> *ourDevice)
{
    if(!ourDevice)
        return paramErr;
    ensure_mouse();
    CursorDevicePtr d = *ourDevice;
    if(!d)
        *ourDevice = devices;
    else if(known(d))
        *ourDevice = d->nextCursorDevice;
    else
        return paramErr;
    return noErr;
}

OSErr Executor::C_CursorDeviceNewDevice(GUEST<CursorDevicePtr> *ourDevice)
{
    if(!ourDevice)
        return paramErr;
    ensure_mouse();
    CursorDevicePtr d = (CursorDevicePtr)NewPtrSysClear(sizeof(CursorDevice));
    if(!d)
        return memFullErr;
    d->doubleClickTime = LM(DoubleTime);
    CursorDevicePtr last = devices;
    while(last->nextCursorDevice)
        last = last->nextCursorDevice;
    last->nextCursorDevice = d;
    *ourDevice = d;
    return noErr;
}

OSErr Executor::C_CursorDeviceDisposeDevice(CursorDevicePtr ourDevice)
{
    if(!known(ourDevice) || ourDevice == devices)
        return paramErr;
    for(CursorDevicePtr d = devices; d; d = d->nextCursorDevice)
        if(d->nextCursorDevice == ourDevice)
        {
            d->nextCursorDevice = ourDevice->nextCursorDevice;
            break;
        }
    DisposePtr((Ptr)ourDevice);
    return noErr;
}
