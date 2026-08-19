// Checking GitHub for a newer Hotswap and installing it.
//
// The whole thing is one blocking call, on purpose. An update is not something
// the user does in the background while browsing mods - they ask for it, they
// wait for it, and then either the app restarts or it tells them why it did
// not. Splitting it into a state machine would buy nothing but a progress bar.
//
// Nothing here touches a game, a mod, or the swap journal. The worst an
// interrupted update can do is leave the release it was fetching un-installed.

#ifndef HOTSWAP_UPDATE_H
#define HOTSWAP_UPDATE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HS_UPDATE_INSTALLED = 0,   // a newer release was fetched and installed
    HS_UPDATE_CURRENT,         // this build is already the newest release
    HS_UPDATE_ERR_NET,         // GitHub could not be reached
    HS_UPDATE_ERR_PARSE,       // it answered with something unreadable
    HS_UPDATE_ERR_NO_ASSET,    // the newest release ships no .cia
    HS_UPDATE_ERR_DOWNLOAD,    // the .cia started arriving and then did not
    HS_UPDATE_ERR_INSTALL      // the system refused to install it
} hs_update_t;

// Asks GitHub for the newest release, and installs it if it is newer than this
// build. `tag` receives the release's version with any leading "v" removed -
// filled in whenever one was read, including when the answer is
// HS_UPDATE_CURRENT, so the caller can say which version it compared against.
hs_update_t hs_update_run(char *tag, size_t tag_cap);

// Arms a jump back into Hotswap itself, for use straight after an install.
// Returns immediately and returns false when this build is running from the
// homebrew launcher, where there is no title to jump back into - the caller
// then has to ask the user to reopen it by hand. Like hs_launch, the jump
// itself happens during the normal shutdown.
bool hs_update_relaunch(void);

const char *hs_update_str(hs_update_t r);

#endif
