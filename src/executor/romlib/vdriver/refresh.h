#if !defined(_refresh_h_)
#define _refresh_h_

#include <ExMacTypes.h>

#define MODULE_NAME vdriver_refresh
#include <base/api-module.h>

namespace Executor
{
extern void set_refresh_rate(int new1);
extern void dequeue_refresh_task(void);
}
#endif /* !_refresh_h_ */
