#pragma once

/* ot_client.h — Open Transport's clients (ot_client.cpp). */

#include <cstdint>

namespace Executor
{
namespace ot
{

struct Client
{
    uint32_t record = 0;   /* its __gOTClientRecord */
    uint32_t kind = 0;     /* InitOpenTransportPriv's: 3 an application */
    uint32_t notifier = 0; /* OTRegisterAsClient's */
    int inits = 0;
};

Client *find_client(uint32_t record);

/* Close what a client left open (CloseOpenTransport, its process's exit).
   Defined with the providers. */
void close_client_providers(uint32_t record);

} // namespace ot
} // namespace Executor
