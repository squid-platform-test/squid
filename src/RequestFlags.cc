/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 73    HTTP Request */

#include "squid.h"
#include "debug/Stream.h"
#include "RequestFlags.h"

#include <iostream>

// When adding new flags, please update cloneAdaptationImmune() as needed.
// returns a partial copy of the flags that includes only those flags
// that are safe for a related (e.g., ICAP-adapted) request to inherit
RequestFlags
RequestFlags::cloneAdaptationImmune() const
{
    // At the time of writing, all flags where either safe to copy after
    // adaptation or were not set at the time of the adaptation. If there
    // are flags that are different, they should be cleared in the clone.
    return *this;
}

void
RequestFlags::disableCacheUse(const char * const reason)
{
    debugs(16, 3, "for " << reason);
    cachable.veto();
    noCache = true; // may already be true
}

