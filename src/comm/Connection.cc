/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#include "squid.h"
#include "base/JobWait.h"
#include "CachePeer.h"
#include "cbdata.h"
#include "comm.h"
#include "comm/Connection.h"
#include "fde.h"
#include "FwdState.h"
#include "neighbors.h"
#include "security/NegotiationHistory.h"
#include "SquidConfig.h"

#include <ostream>

InstanceIdDefinitions(Comm::Connection, "conn", uint64_t);

class CachePeer;
bool
Comm::IsConnOpen(const Comm::ConnectionPointer &conn)
{
    return conn != nullptr && conn->isOpen();
}

Comm::Connection::Connection() :
    peerType(HIER_NONE),
    fd(-1),
    tos(0),
    nfmark(0),
    flags(COMM_NONBLOCKING),
    peer_(nullptr),
    startTime_(squid_curtime),
    tlsHistory(nullptr)
{}

Comm::Connection::~Connection()
{
    if (fd >= 0) {
        if (flags & COMM_ORPHANED) {
            debugs(5, 5, "closing orphan: " << *this);
        } else {
            static uint64_t losses = 0;
            ++losses;
            debugs(5, 4, "BUG #3329: Lost orphan #" << losses << ": " << *this);
        }
        close();
    }

    cbdataReferenceDone(peer_);

    delete tlsHistory;
}

Comm::ConnectionPointer
Comm::Connection::cloneProfile() const
{
    const ConnectionPointer clone = new Comm::Connection;
    auto &c = *clone; // optimization

    /*
     * Copy or excuse each data member. Excused members do not belong to a
     * Connection configuration profile because their values cannot be reused
     * across (co-existing) Connection objects and/or are tied to their own
     * object lifetime.
     */

    c.setAddrs(local, remote);
    c.peerType = peerType;
    // fd excused
    c.tos = tos;
    c.nfmark = nfmark;
    c.nfConnmark = nfConnmark;
    // COMM_ORPHANED is not a part of connection opening instructions
    c.flags = flags & ~COMM_ORPHANED;

#if USE_SQUID_EUI
    // These are currently only set when accepting connections and never used
    // for establishing new ones, so this copying is currently in vain, but,
    // technically, they can be a part of connection opening instructions.
    c.remoteEui48 = remoteEui48;
    c.remoteEui64 = remoteEui64;
#endif

    // id excused
    c.peer_ = cbdataReference(getPeer());
    // startTime_ excused
    // tlsHistory excused

    debugs(5, 5, this << " made " << c);
    assert(!c.isOpen());
    return clone;
}

void
Comm::Connection::close()
{
    if (isOpen()) {
        comm_close(fd);
        noteClosure();
    }
}

void
Comm::Connection::noteClosure()
{
    if (isOpen()) {
        fd = -1;
        if (CachePeer *p=getPeer())
            peerConnClosed(p);
    }
}

CachePeer *
Comm::Connection::getPeer() const
{
    if (cbdataReferenceValid(peer_))
        return peer_;

    return nullptr;
}

void
Comm::Connection::setPeer(CachePeer *p)
{
    /* set to self. nothing to do. */
    if (getPeer() == p)
        return;

    cbdataReferenceDone(peer_);
    if (p) {
        peer_ = cbdataReference(p);
    }
}

bool
Comm::Connection::toGoneCachePeer() const
{
    return peer_ && !cbdataReferenceValid(peer_);
}

time_t
Comm::Connection::timeLeft(const time_t idleTimeout) const
{
    if (!Config.Timeout.pconnLifetime)
        return idleTimeout;

    const time_t lifeTimeLeft = lifeTime() < Config.Timeout.pconnLifetime ? Config.Timeout.pconnLifetime - lifeTime() : 1;
    return min(lifeTimeLeft, idleTimeout);
}

Security::NegotiationHistory *
Comm::Connection::tlsNegotiations()
{
    if (!tlsHistory)
        tlsHistory = new Security::NegotiationHistory;
    return tlsHistory;
}

time_t
Comm::Connection::connectTimeout(const time_t fwdStart) const
{
    // a connection opening timeout (ignoring forwarding time limits for now)
    const CachePeer *peer = getPeer();
    const auto ctimeout = peer ? peer->connectTimeout() : Config.Timeout.connect;

    // time we have left to finish the whole forwarding process
    const time_t fwdTimeLeft = FwdState::ForwardTimeout(fwdStart);

    // The caller decided to connect. If there is no time left, to protect
    // connecting code from trying to establish a connection while a zero (i.e.,
    // "immediate") timeout notification is firing, ensure a positive timeout.
    // XXX: This hack gives some timed-out forwarding sequences more time than
    // some sequences that have not quite reached the forwarding timeout yet!
    const time_t ftimeout = fwdTimeLeft ? fwdTimeLeft : 5; // seconds

    return min(ctimeout, ftimeout);
}

ScopedId
Comm::Connection::codeContextGist() const {
    return id.detach();
}

std::ostream &
Comm::Connection::detailCodeContext(std::ostream &os) const
{
    return os << Debug::Extra << "connection: " << *this;
}

std::ostream &
Comm::operator << (std::ostream &os, const Connection &conn)
{
    os << conn.id;
    if (!conn.local.isNoAddr() || conn.local.port())
        os << " local=" << conn.local;
    if (!conn.remote.isNoAddr() || conn.remote.port())
        os << " remote=" << conn.remote;
    if (conn.peerType)
        os << ' ' << hier_code_str[conn.peerType];
    if (conn.fd >= 0)
        os << " FD " << conn.fd;
    if (conn.flags != COMM_UNSET)
        os << " flags=" << conn.flags;
    return os;
}

