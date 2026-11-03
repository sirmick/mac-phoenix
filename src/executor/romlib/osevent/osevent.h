#if !defined(_RSYS_OSEVENT_H_)
#define _RSYS_OSEVENT_H_

#include <EventMgr.h>
#include <OSEvent.h>
#include <ProcessMgr.h>

namespace Executor
{
extern OSErr ROMlib_PPostEvent(INTEGER evcode, LONGINT evmsg,
                                  GUEST<EvQElPtr> *qelp, LONGINT when, Point where, INTEGER butmods);

extern Ptr ROMlib_kchr_ptr(void);

extern void invalidate_kchr_ptr(void);

enum
{
    NUMPAD_ENTER = 0x3
};

enum
{
    sizeof_KeyMap = 16
}; /* can't use sizeof(LM(KeyMap)) */

extern void ROMlib_eventinit();

extern void display_keyboard_choices(void);

extern bool ROMlib_set_keyboard(const char *keyboardname);
extern Boolean ROMlib_bewaremovement;
extern void ROMlib_showhidecursor(void);

extern uint16_t ROMlib_right_to_left_key_map(uint16_t what);

bool ROMlib_GetKey(uint8_t mkvkey);
void ROMlib_SetKey(uint8_t mkvkey, bool down);
short ROMlib_GetModifiers();

void ROMlib_SetAutokey(uint32_t message);

/* MacPhoenix: the host's scroll wheel (osevent/wheel.cpp): lines turned
   (right and down positive); started from GetNextEvent; while its click
   is held, the mouse the Event Manager reports and Button's answer. */
void ROMlib_wheel_add(int dh, int dv);
void ROMlib_wheel_start();
void ROMlib_wheel_mouse(EventRecord *evt);
bool ROMlib_wheel_button(bool *down);

extern bool hle_get_event(EventRecord *evt, bool remflag);
extern void hle_init(void);
extern void hle_reinit(void);
extern void hle_reset(void);
/* MacPhoenix: high-level events waiting for that process; drop them. */
extern bool ROMlib_hle_pending(const ProcessSerialNumber *psn);
extern void ROMlib_hle_forget(const ProcessSerialNumber *psn);
extern OSErr ROMlib_hle_post_from_system(const ProcessSerialNumber *to,
                                         OSType event_class, OSType event_id,
                                         int32_t refcon, Ptr msg, int32_t len);
}
#endif
