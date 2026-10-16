#pragma once

/* ot_provider.h — Open Transport's providers (ot_provider.cpp): endpoints
 * on host sockets and Internet services (ot_inet.cpp), their modes,
 * notifiers and events. */

#include "ot_internal.h"

#include <deque>
#include <functional>
#include <string>

namespace Executor
{
namespace ot
{

/* OT's event codes (OpenTransport.h, OpenTptInternet.h). */
enum : uint32_t
{
    T_LISTEN = 0x0001,
    T_CONNECT = 0x0002,
    T_DATA = 0x0004,
    T_EXDATA = 0x0008,
    T_DISCONNECT = 0x0010,
    T_UDERR = 0x0040,
    T_ORDREL = 0x0080,
    T_GODATA = 0x0100,
    T_PASSCON = 0x1000,
    T_BINDCOMPLETE = 0x20000001,
    T_UNBINDCOMPLETE = 0x20000002,
    T_ACCEPTCOMPLETE = 0x20000003,
    T_DISCONNECTCOMPLETE = 0x20000005,
    T_OPTMGMTCOMPLETE = 0x20000006,
    T_OPENCOMPLETE = 0x20000007,
    T_DNRSTRINGTOADDRCOMPLETE = 0x10000001,
    T_DNRADDRTONAMECOMPLETE = 0x10000002,
};

/* Endpoint states. */
enum : uint32_t
{
    T_UNBND = 1,
    T_IDLE = 2,
    T_OUTCON = 3,
    T_INCON = 4,
    T_DATAXFER = 5,
    T_OUTREL = 6,
    T_INREL = 7,
};

enum class Kind : uint8_t
{
    TCP,
    UDP,
    Inet, /* Internet services: the resolver */
};

struct Provider
{
    uint32_t ref = 0;    /* the ProviderRef the client holds */
    uint32_t client = 0; /* its client record */
    Kind kind = Kind::TCP;

    /* Modes: a new provider is synchronous and non-blocking. */
    bool async = false;
    bool blocking = false;
    bool ack_sends = false;

    /* OTInstallNotifier's (or the async open's) notifier and context, and
       the A5 world to call it in (the client's when it was installed). */
    uint32_t notifier = 0, context = 0, a5 = 0;
    bool in_notifier = false;

    /* Endpoints. */
    uint32_t state = T_UNBND;
    int fd = -1;
    bool listener = false;
    uint32_t qlen = 0;
    bool connecting = false;     /* a connect in progress */
    uint32_t look = 0;           /* the event OTLook reports, if any */
    bool data_armed = true;      /* T_DATA not yet sent for what's waiting */
    bool eof_seen = false;       /* the peer's FIN */
    bool flow_blocked = false;   /* a send hit EAGAIN: T_GODATA when it can */
    bool listen_armed = true;
    struct Pending
    {
        int fd;
        uint32_t seq;
        uint32_t host;
        uint16_t port;
    };
    std::deque<Pending> incoming; /* connections OTListen has handed out */
    uint32_t next_seq = 1;
};

Provider *find_provider(uint32_t ref);
/* A new provider for a client; ref allocated. */
Provider &new_provider(uint32_t client, Kind kind);
void close_provider(uint32_t ref);

/* Queue an event for the provider's notifier (delivered at interrupt
   time, never inside a call); without a notifier it is dropped. */
void post(Provider &p, uint32_t code, int32_t result, uint32_t cookie);
/* Raise the OT interrupt (any thread). */
void wake();
/* Work for the interrupt from other parts (the resolver's results). */
void add_pump_hook(std::function<void()> hook);

/* The A5 world the client is running in now. */
uint32_t current_a5();

} // namespace ot
} // namespace Executor
