#pragma once

#include "vdriver.h"
#include <rsys/filesystem.h>
#include <time/time.h>

namespace Executor
{

class EventRecorder : public EventSink
{
    fs::ofstream out;
    int lastH = -1, lastV = -1;
public:
    EventRecorder(fs::path fn) : out(fn) {}

    virtual void mouseButtonEvent(bool down) override;
    virtual void mouseMoved(int h, int v) override;
    virtual void keyboardEvent(bool down, unsigned char mkvkey) override;
};


class EventPlayback : public EventSink
{
    fs::ifstream in;
    bool playbackActive;
    unsigned long nextEvent;

    static EventPlayback *instance;
public:
    EventPlayback(fs::path fn);

    virtual void mouseButtonEvent(bool down) override;
    virtual void mouseMoved(int h, int v) override;
    virtual void keyboardEvent(bool down, unsigned char mkvkey) override;

    void pumpEvents();

    static EventPlayback* getInstance();
};

}