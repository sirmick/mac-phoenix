/*
 *  bridge_cfg.h — guest-side helper for reading the per-instance
 *  bridge config the host writes into :System Folder:Preferences:
 *  MacPhoenix.cfg, and building Pascal-string paths under the
 *  bridge dir.
 *
 *  Used by BridgeAgent and MacBrowser. Both apps:
 *    1. Call br_cfg_load() once at startup (idempotent; cached).
 *    2. Build paths via br_cfg_path(out, leaf) for each FSMakeFSSpec.
 *
 *  If MacPhoenix.cfg is missing or unreadable (legacy host or
 *  --bridge disabled), the helper falls back to the historical
 *  "Host:MacPhoenix" prefix so existing single-instance setups
 *  keep working without an updated host binary.
 *
 *  All strings here are Pascal (length-prefixed); callers can hand
 *  them straight to FSMakeFSSpec.
 */
#ifndef GUEST_BRIDGE_CFG_H
#define GUEST_BRIDGE_CFG_H

/* Read :System Folder:Preferences:MacPhoenix.cfg from the boot disk.
 * Caches the parsed bridge dir in a static; subsequent calls are
 * a no-op. Always succeeds — falls back to "Host:MacPhoenix" on any
 * read/parse error so the guest can still talk to legacy hosts. */
void br_cfg_load(void);

/* Write `Host:MacPhoenix:<pid>:<leaf>` (or the legacy fallback
 * `Host:MacPhoenix:<leaf>`) into `out` as a Pascal string. `leaf`
 * is a plain C string (e.g. "bridge_heartbeat"). `out` must have
 * room for 256 bytes. */
void br_cfg_path(unsigned char *out, const char *leaf);

/* Build a path under the persistent MacBrowser data folder:
 * `Host:macbrowser:<leaf>` as a Pascal string. Independent of the
 * per-instance bridge dir — bookmarks/downloads survive a PID change.
 * `leaf` may itself contain `:` separators (e.g. "Downloads:foo.zip").
 * `out` must have room for 256 bytes. */
void br_macbrowser_path(unsigned char *out, const char *leaf);

#endif  /* GUEST_BRIDGE_CFG_H */
