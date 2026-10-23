/*
 * text_feed.h - the strings QuickDraw draws, with where they landed on the
 * screen (ROM machines, 68k and PPC). See text_feed.cpp.
 */
#pragma once

#include <cstdint>
#include <string>

/* Patch _StdText, _CopyBits and _ScrollRect in the trap table
 * (PatchAfterStartup, once per boot). */
void text_feed_install();

/* The bridge trap ($ABE5, guest_bridge.h): its trap table entry pointed at
 * [EmulOp BRIDGE_COMM; rts], if the System leaves it unimplemented. Installed
 * where the text feed is (after startup, every ROM machine). */
void bridge_trap_install();

/* BridgeAgent launched from the bridge dir on the Host volume by a Time
 * Manager task and a Notification Manager response procedure, once per
 * boot (bridge_trap_install calls it). */
void bridge_launcher_install();

/* The patches' EmulOps: before StdText runs, and after the original of
 * which (D0: 1 StdText, 2 CopyBits, 3 ScrollRect) returned. sp: A7 at the
 * EmulOp (the caller's return address on top), a5: A5. */
void text_feed_pre(uint32_t sp, uint32_t a5);
void text_feed_post(uint32_t sp, uint32_t a5, int which);

/* For /api/text. query "" (or "raw=1", unmerged): the text on screen now,
 * top to bottom; "since=N": every draw after sequence number N, on screen
 * or not. */
/* max_bytes (0: none): the draw log (since=) drops its oldest draws to fit,
 * saying how many ("dropped"). */
std::string text_feed_json(const std::string &query, size_t max_bytes = 0);

extern bool text_feed_enabled;
