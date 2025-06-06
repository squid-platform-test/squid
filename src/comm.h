/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_COMM_H
#define SQUID_SRC_COMM_H

#include "comm/IoCallback.h"
#include "CommCalls.h"
#include "StoreIOBuffer.h"

namespace Ip
{
class Address;
}

bool comm_iocallbackpending(void); /* inline candidate */

int commSetNonBlocking(int fd);
int commUnsetNonBlocking(int fd);

/// On platforms where FD_CLOEXEC is defined, close the given descriptor during
/// a function call from the exec(3) family. Otherwise, do nothing; the platform
/// itself may close-on-exec by default (e.g., MS Win32 is said to do that at
/// https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873); other
/// platforms are unsupported. Callers that want close-on-exec behavior must
/// call this function on all platforms and are not responsible for the outcome
/// on platforms without FD_CLOEXEC.
void commSetCloseOnExec(int fd);

void _comm_close(int fd, char const *file, int line);
#define comm_close(x) (_comm_close((x), __FILE__, __LINE__))
void old_comm_reset_close(int fd);
void comm_reset_close(const Comm::ConnectionPointer &conn);

int comm_connect_addr(int sock, const Ip::Address &addr);
void comm_init(void);
void comm_exit(void);

int comm_open(int, int, Ip::Address &, int, const char *note);
int comm_open_uds(int sock_type, int proto, struct sockaddr_un* addr, int flags);
/// update Comm state after getting a comm_open() FD from another process
void comm_import_opened(const Comm::ConnectionPointer &, const char *note, struct addrinfo *AI);

/**
 * Open a port specially bound for listening or sending through a specific port.
 * Please use for all listening sockets and bind() outbound sockets.
 *
 * It will open a socket bound for:
 *  - IPv4 if IPv6 is disabled or address is IPv4-native.
 *  - IPv6 if address is IPv6-native
 *  - IPv6 dual-stack mode if able to open [::]
 *
 * When an open performs failover it update the given address to feedback
 * the new IPv4-only status of the socket. Further displays of the IP
 * (in debugs or cachemgr) will occur in Native IPv4 format.
 * A reconfigure is needed to reset the stored IP in most cases and attempt a port re-open.
 */
int comm_open_listener(int sock_type, int proto, Ip::Address &addr, int flags, const char *note);
void comm_open_listener(int sock_type, int proto, Comm::ConnectionPointer &conn, const char *note);

unsigned short comm_local_port(int fd);

int comm_udp_sendto(int sock, const Ip::Address &to, const void *buf, int buflen);
void commCallCloseHandlers(int fd);

/// clear a timeout handler by FD number
void commUnsetFdTimeout(int fd);

/**
 * Set or clear the timeout for some action on an active connection.
 * API to replace commSetTimeout() when a Comm::ConnectionPointer is available.
 */
void commSetConnTimeout(const Comm::ConnectionPointer &, time_t seconds, AsyncCall::Pointer &);
void commUnsetConnTimeout(const Comm::ConnectionPointer &);

int ignoreErrno(int);
void commCloseAllSockets(void);
void checkTimeouts(void);

AsyncCall::Pointer comm_add_close_handler(int fd, CLCB *, void *);
void comm_add_close_handler(int fd, AsyncCall::Pointer &);
void comm_remove_close_handler(int fd, CLCB *, void *);
void comm_remove_close_handler(int fd, AsyncCall::Pointer &);

int comm_udp_recvfrom(int fd, void *buf, size_t len, int flags, Ip::Address &from);
int comm_udp_recv(int fd, void *buf, size_t len, int flags);
ssize_t comm_udp_send(int s, const void *buf, size_t len, int flags);
bool comm_has_incomplete_write(int);

/** The read channel has closed and the caller does not expect more data
 * but needs to detect connection aborts. The current detection method uses
 * 0-length reads: We read until the error occurs or the writer closes
 * the connection. If there is a read error, we close the connection.
 */
void commStartHalfClosedMonitor(int fd);
bool commHasHalfClosedMonitor(int fd);
// XXX: remove these wrappers which minimize client_side.cc changes in a commit
inline void commMarkHalfClosed(int fd) { commStartHalfClosedMonitor(fd); }
inline bool commIsHalfClosed(int fd) { return commHasHalfClosedMonitor(fd); }

/* A comm engine that calls comm_select */

class CommSelectEngine : public AsyncEngine
{

public:
    int checkEvents(int timeout) override;
};

#endif /* SQUID_SRC_COMM_H */

