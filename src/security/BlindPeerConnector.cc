/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#include "squid.h"
#include "AccessLogEntry.h"
#include "CachePeer.h"
#include "comm/Connection.h"
#include "errorpage.h"
#include "fde.h"
#include "HttpRequest.h"
#include "neighbors.h"
#include "security/BlindPeerConnector.h"
#include "security/NegotiationHistory.h"
#include "SquidConfig.h"

CBDATA_NAMESPACED_CLASS_INIT(Security, BlindPeerConnector);

Security::FuturePeerContext *
Security::BlindPeerConnector::peerContext() const
{
    const auto peer = serverConnection()->getPeer();
    if (peer && peer->secure.encryptTransport)
        return peer->securityContext();

    return Config.ssl_client.defaultPeerContext;
}

bool
Security::BlindPeerConnector::initialize(Security::SessionPointer &serverSession)
{
    if (!Security::PeerConnector::initialize(serverSession)) {
        debugs(83, 5, "Security::PeerConnector::initialize failed");
        return false;
    }

    const CachePeer *peer = serverConnection()->getPeer();
    if (peer && peer->secure.encryptTransport) {
        assert(peer);

        // NP: domain may be a raw-IP but it is now always set
        assert(!peer->secure.sslDomain.isEmpty());

#if USE_OPENSSL
        // const loss is okay here, ssl_ex_index_server is only read and not assigned a destructor
        SBuf *host = new SBuf(peer->secure.sslDomain);
        SSL_set_ex_data(serverSession.get(), ssl_ex_index_server, host);
        Ssl::setClientSNI(serverSession.get(), host->c_str());

        Security::SetSessionResumeData(serverSession, peer->sslSession);
    } else {
        SBuf *hostName = new SBuf(request->url.host());
        SSL_set_ex_data(serverSession.get(), ssl_ex_index_server, (void*)hostName);
        Ssl::setClientSNI(serverSession.get(), hostName->c_str());
#endif
    }

    debugs(83, 5, "success");
    return true;
}

void
Security::BlindPeerConnector::noteNegotiationDone(ErrorState *error)
{
    auto *peer = serverConnection()->getPeer();

    if (error) {
        debugs(83, 5, "error=" << (void*)error);
        // XXX: FwdState calls NoteOutgoingConnectionSuccess() after an OK TCP connect, but
        // we call noteFailure() if SSL failed afterwards. Is that OK?
        // It is not clear whether we should call noteSuccess()/noteFailure()/etc.
        // based on TCP results, SSL results, or both. And the code is probably not
        // consistent in this aspect across tunnelling and forwarding modules.
        if (peer && peer->secure.encryptTransport)
            peer->noteFailure();
        return;
    }

    if (peer && peer->secure.encryptTransport) {
        const int fd = serverConnection()->fd;
        Security::MaybeGetSessionResumeData(fd_table[fd].ssl, peer->sslSession);
    }
}

Security::BlindPeerConnector::BlindPeerConnector(HttpRequestPointer &aRequest,
        const Comm::ConnectionPointer &aServerConn,
        const AsyncCallback<EncryptorAnswer> &aCallback,
        const AccessLogEntryPointer &alp,
        time_t timeout) :
    AsyncJob("Security::BlindPeerConnector"),
    Security::PeerConnector(aServerConn, aCallback, alp, timeout)
{
    request = aRequest;
}
