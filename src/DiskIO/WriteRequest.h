/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_DISKIO_WRITEREQUEST_H
#define SQUID_SRC_DISKIO_WRITEREQUEST_H

#include "base/RefCount.h"
#include "cbdata.h"
#include "mem/forward.h"

class WriteRequest : public RefCountable
{
    CBDATA_CLASS(WriteRequest);

public:
    typedef RefCount<WriteRequest> Pointer;
    WriteRequest(char const *buf, off_t offset, size_t len, FREE *);
    ~WriteRequest() override {}

    char const *buf;
    off_t offset;
    size_t len;
    FREE *free_func;
};

#endif /* SQUID_SRC_DISKIO_WRITEREQUEST_H */

