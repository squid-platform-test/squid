/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 54    Interprocess Communication */

#ifndef SQUID_SRC_IPC_SHAREDLISTEN_H
#define SQUID_SRC_IPC_SHAREDLISTEN_H

#include "base/AsyncCall.h"
#include "base/Subscription.h"
#include "ip/Address.h"
#include "ipc/QuestionerId.h"
#include "ipc/RequestId.h"
#include "ipc/StartListening.h"

namespace Ipc
{

/// "shared listen" is when concurrent processes are listening on the same fd

/// Comm::ConnAcceptor parameters holder
/// all the details necessary to recreate a Comm::Connection and fde entry for the kid listener FD
class OpenListenerParams
{
public:
    bool operator <(const OpenListenerParams &p) const; ///< useful for map<>

    // bits to re-create the fde entry
    int sock_type = 0;
    int proto = 0;
    int fdNote = 0; ///< index into fd_note() comment strings

    // bits to re-create the listener Comm::Connection descriptor
    Ip::Address addr; ///< will be memset and memcopied
    int flags = 0;
};

class TypedMsgHdr;

/// a request for a listen socket with given parameters
class SharedListenRequest
{
public:
    SharedListenRequest(const OpenListenerParams &, RequestId aMapId); ///< sender's constructor
    explicit SharedListenRequest(const TypedMsgHdr &hdrMsg); ///< from recvmsg()
    void pack(TypedMsgHdr &hdrMsg) const; ///< prepare for sendmsg()

public:
    int requestorId; ///< kidId of the requestor

    OpenListenerParams params; ///< actual comm_open_sharedListen() parameters

    RequestId mapId; ///< to map future response to the requestor's callback
};

/// a response to SharedListenRequest
class SharedListenResponse
{
public:
    SharedListenResponse(int fd, int errNo, RequestId aMapId); ///< sender's constructor
    explicit SharedListenResponse(const TypedMsgHdr &hdrMsg); ///< from recvmsg()
    void pack(TypedMsgHdr &hdrMsg) const; ///< prepare for sendmsg()

    /// for Mine() tests
    QuestionerId intendedRecepient() const { return mapId.questioner(); }

public:
    int fd; ///< opened listening socket or -1
    int errNo; ///< errno value from comm_open_sharedListen() call
    RequestId mapId; ///< to map future response to the requestor's callback
};

/// prepare and send SharedListenRequest to Coordinator
void JoinSharedListen(const OpenListenerParams &, StartListeningCallback &);

/// process Coordinator response to SharedListenRequest
void SharedListenJoined(const SharedListenResponse &response);

} // namespace Ipc;

#endif /* SQUID_SRC_IPC_SHAREDLISTEN_H */

