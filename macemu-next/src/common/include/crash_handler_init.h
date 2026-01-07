/*
 *  crash_handler_init.h - Install crash handlers for macemu-next
 */

#ifndef CRASH_HANDLER_INIT_H
#define CRASH_HANDLER_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

// Install crash handlers for all fatal signals
void install_crash_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_HANDLER_INIT_H */
