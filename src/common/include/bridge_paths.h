/*
 *  bridge_paths.h — shared file leaf names for bridge IPC.
 *
 *  Both host (mac-phoenix) and guest (BridgeAgent + MacBrowser) reference
 *  the same set of files under a per-instance bridge directory. The host's
 *  filesystem path is `<extfs>/MacPhoenix/<pid>/<leaf>`; the guest sees
 *  the same files at `Host:MacPhoenix:<pid>:<leaf>`.
 *
 *  Why a shared header:
 *    - One place to grep when the protocol changes.
 *    - Compile-time check that both sides spell the same name.
 *    - Drop-in for future leaf additions.
 *
 *  This header is plain C macros — works under both Retro68 m68k C and
 *  host C++. It deliberately does NOT include the path prefix, since the
 *  host needs slash-style and the guest needs colon-style separators.
 */
#ifndef BRIDGE_PATHS_H
#define BRIDGE_PATHS_H

/* Volume + parent folder names. Both sides share these.
 *
 * BR_VOLUME      — Mac volume the host's first --extfs root surfaces as.
 *                  Hardcoded "Host" since BasiliskII/SheepShaver days; see
 *                  src/core/user_strings.cpp:STR_EXTFS_VOLUME_NAME.
 * BR_PARENT      — Folder under the volume that namespaces our IPC files.
 *                  Per-instance pid subfolder lives inside this.
 */
#define BR_VOLUME            "Host"
#define BR_PARENT            "MacPhoenix"

/* Per-instance config that the guest reads at startup to learn its
 * paired host's pid. Lives in :System Folder:Preferences: of whatever
 * disk the host installs BridgeAgent into. Same disk → same boot →
 * the guest's FindFolder(kPreferencesFolderType, kOnSystemDisk) finds
 * exactly the cfg the host just wrote. */
#define BR_CFG_LEAF          "MacPhoenix.cfg"

/* Bridge-protocol files (BridgeAgent ↔ host). All live under the
 * per-instance bridge dir: <bridge_dir>/<leaf> on host,
 * Host:MacPhoenix:<pid>:<leaf> on guest. */
#define BR_FILE_HEARTBEAT    "bridge_heartbeat"
#define BR_FILE_LOADED       "bridge_loaded"
#define BR_FILE_STEP         "bridge_step"
#define BR_FILE_CMD          "_bridge_cmd"
#define BR_FILE_RESULT       "_bridge_result"
#define BR_FILE_CLIPBOARD    "_bridge_clipboard"
#define BR_FILE_NETCFG       "netcfg.txt"
#define BR_FILE_NETWORKINFO  "NetworkInfo.txt"

/* MacBrowser-specific. Same per-instance bridge dir. */
#define BR_FILE_BROWSER_SHM  "browser_shm.txt"
#define BR_FILE_BROWSER_LOG  "MacBrowser.log"
#define BR_DIR_DOWNLOADS     "Downloads"

#endif  /* BRIDGE_PATHS_H */
