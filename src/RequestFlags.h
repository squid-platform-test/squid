/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 73    HTTP Request */

#ifndef SQUID_SRC_REQUESTFLAGS_H
#define SQUID_SRC_REQUESTFLAGS_H

#include "base/SupportOrVeto.h"

/** request-related flags
 *
 * Contains both flags marking a request's current state,
 * and flags requesting some processing to be done at a later stage.
 * TODO: better distinguish the two cases.
 */
class RequestFlags
{
public:
    /** true if the response to this request may not be READ from cache */
    bool noCache = false;
    /** request is if-modified-since */
    bool ims = false;
    /** request is authenticated */
    bool auth = false;
    /** do not use keytabs for peer Kerberos authentication */
    bool auth_no_keytab = false;

    /// whether the response may be stored in the cache
    SupportOrVeto cachable;

    /** the request can be forwarded through the hierarchy */
    bool hierarchical = false;
    /** a loop was detected on this request */
    bool loopDetected = false;
    /** the connection can be kept alive */
    bool proxyKeepalive = false;
    /** content has expired, need to refresh it */
    bool refresh = false;
    /** request was redirected by redirectors */
    bool redirected = false;
    /** the requested object needs to be validated. See client_side_reply.cc
     * for further information.
     */
    bool needValidation = false;
    /** whether we should fail if validation fails */
    bool failOnValidationError = false;
    /** reply is stale if it is a hit */
    bool staleIfHit = false;
    /** request to override no-cache directives
     *
     * always use noCacheHack() for reading.
     * \note only meaningful if USE_HTTP_VIOLATIONS is defined at build time
     */
    bool nocacheHack = false;
    /** this request is accelerated (reverse-proxy) */
    bool accelerated = false;
    /** if set, ignore Cache-Control headers */
    bool ignoreCc = false;
    /** set for intercepted requests */
    bool intercepted = false;
    /** set if the Host: header passed verification */
    bool hostVerified = false;
    /// Set for requests handled by a "tproxy" port.
    bool interceptTproxy = false;
    /// The client IP address should be spoofed when connecting to the web server.
    /// This applies to TPROXY traffic that has not had spoofing disabled through
    /// the spoof_client_ip squid.conf ACL.
    bool spoofClientIp = false;

    /// whether the request targets a /squid-internal- resource (e.g., a MIME
    /// icon or a cache manager page) served by this Squid instance
    /// TODO: Rename to avoid a false implication that this flag is true for
    /// requests for /squid-internal- resources served by other Squid instances.
    bool internal = false;

    /** if set, request to try very hard to keep the connection alive */
    bool mustKeepalive = false;
    /** set if the request wants connection oriented auth */
    bool connectionAuth = false;
    /** set if connection oriented auth can not be supported */
    bool connectionAuthDisabled = false;
    // XXX This is set in clientCheckPinning but never tested
    /** Request wants connection oriented auth */
    bool connectionProxyAuth = false;
    /** set if the request was sent on a pinned connection */
    bool pinned = false;
    /** Authentication was already sent upstream (e.g. due tcp-level auth) */
    bool authSent = false;
    /** Deny direct forwarding unless overridden by always_direct
     * Used in accelerator mode */
    bool noDirect = false;
    /** Reply with chunked transfer encoding */
    bool chunkedReply = false;
    /** set if stream error has occurred */
    bool streamError = false;
    /** internal ssl-bump request to get server cert */
    bool sslPeek = false;
    /** set if X-Forwarded-For checking is complete
     *
     * do not read directly; use doneFollowXff for reading
     */
    bool done_follow_x_forwarded_for = false;
    /** set for ssl-bumped requests */
    bool sslBumped = false;
    /// carries a representation of an FTP command [received on ftp_port]
    bool ftpNative = false;
    bool destinationIpLookedUp = false;
    /** request to reset the TCP stream */
    bool resetTcp = false;
    /** set if the request is ranged */
    bool isRanged = false;

    /// whether to forward via TunnelStateData (instead of FwdState)
    bool forceTunnel = false;

    /** clone the flags, resetting to default those which are not safe in
     *  a related (e.g. ICAP-adapted) request.
     */
    RequestFlags cloneAdaptationImmune() const;

    // if FOLLOW_X_FORWARDED_FOR is not set, we always return "done".
    bool doneFollowXff() const {
        return done_follow_x_forwarded_for || !FOLLOW_X_FORWARDED_FOR;
    }

    // if USE_HTTP_VIOLATIONS is not set, never allow this
    bool noCacheHack() const {
        return USE_HTTP_VIOLATIONS && nocacheHack;
    }

    /// ban satisfying the request from the cache and ban storing the response
    /// in the cache
    /// \param reason summarizes the marking decision context (for debugging)
    void disableCacheUse(const char *reason);
};

#endif /* SQUID_SRC_REQUESTFLAGS_H */

