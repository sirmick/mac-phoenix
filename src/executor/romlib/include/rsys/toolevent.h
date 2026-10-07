#include <atomic>
#if !defined(_TOOLEVENT_H_)
#define _TOOLEVENT_H_

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

namespace Executor
{
/* MacPhoenix: true once the application has called Get/WaitNextEvent. */
extern std::atomic<bool> ROMlib_app_polled_events;

extern void dofloppymount(void);
extern Boolean ROMlib_beepedonce;

extern void sendsuspendevent(void);
extern void sendresumeevent(bool cvtclip);
extern void ROMlib_send_quit(void);
extern void ROMlib_alarmoffmbar(void);

extern int ROMlib_right_button_modifier;   /* in parse.ypp */
}

#endif /* !_TOOLEVENT_H_ */
