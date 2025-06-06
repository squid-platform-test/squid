/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 16    Cache Manager API */

#ifndef SQUID_SRC_MGR_REQUEST_H
#define SQUID_SRC_MGR_REQUEST_H

#include "comm/forward.h"
#include "ipc/forward.h"
#include "ipc/Request.h"
#include "mgr/ActionParams.h"

namespace Mgr
{

/// cache manager request
class Request: public Ipc::Request
{
public:
    Request(int aRequestorId, Ipc::RequestId, const Comm::ConnectionPointer &aConn,
            const ActionParams &aParams);

    explicit Request(const Ipc::TypedMsgHdr& msg); ///< from recvmsg()
    /* Ipc::Request API */
    void pack(Ipc::TypedMsgHdr& msg) const override;
    Pointer clone() const override;

public:
    Comm::ConnectionPointer conn; ///< HTTP client connection descriptor

    ActionParams params; ///< action name and parameters
};

} // namespace Mgr

#endif /* SQUID_SRC_MGR_REQUEST_H */

