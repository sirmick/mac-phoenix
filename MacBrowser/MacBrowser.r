/* SIZE resource: small heap, no special flags needed. The spike is a
 * foreground app — it doesn't need canBackground or HLE awareness. */
#include "Types.r"
#include "Processes.r"
#include "Finder.r"

resource 'SIZE' (-1, purgeable) {
	reserved,
	ignoreSuspendResumeEvents,
	reserved,
	cannotBackground,
	needsActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	notHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	/* sizeof(BrowserShm) ≈ 1.7 MiB plus headroom for Toolbox + heap. */
	4 * 1024 * 1024,
	4 * 1024 * 1024
};

/* Icon family — generated from tools/icons/macbrowser-{32,16}.png by
 * tools/png2icn.py. ICN# (32×32 1-bit + mask) and ics# (16×16) only;
 * Finder will use those for both the icon view and the small list
 * view. Color variants (icl4 / icl8) are a follow-up — modern System
 * 7+ falls back to black-on-white cleanly when missing. */
#include "icons.r"

/* BNDL + FREF + creator signature. Without these, Finder shows the
 * generic "blank document" icon for the app even though ICN# 128
 * exists. The creator code 'MxBr' must match the -c flag the Rez
 * step passes to set the file's Finder info. */
resource 'BNDL' (128) {
	'MxBr', 0,
	{
		'FREF', { 0, 128 },
		'ICN#', { 0, 128 }
	}
};

resource 'FREF' (128) {
	'APPL', 0, ""
};

/* Creator-signature resource: holds the version blurb the Finder
 * shows in Get Info → Where. The `type ... as 'STR '` tells Rez
 * to treat 'MxBr' as a Pascal-string resource. */
type 'MxBr' as 'STR ';
resource 'MxBr' (0, "Owner resource") {
	"MacBrowser — modern web in System 7. © 2026 mac-phoenix"
};
