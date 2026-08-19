// The version this build calls itself.
//
// It is compared against the tag of the newest GitHub release, so it has to
// match the tag the release is cut from - `v` prefix stripped. Bump it in the
// same commit that bumps CHANGELOG.md, or the app will offer the user an
// update to the version it is already running.

#ifndef HOTSWAP_VERSION_H
#define HOTSWAP_VERSION_H

#define HOTSWAP_VERSION "1.2.0"

#endif
