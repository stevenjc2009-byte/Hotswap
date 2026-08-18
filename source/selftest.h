#ifndef MK7SWAP_SELFTEST_H
#define MK7SWAP_SELFTEST_H

#include "core/swap.h"

// Runs the full swap cycle against the real SD archive and writes
// sdmc:/mk7swap/selftest.log. Returns the number of failed checks.
int selftest_run(const hs_ctx_t *ctx);

#endif
