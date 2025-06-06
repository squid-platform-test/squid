/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 76    Internal Squid Object handling */

#include "squid.h"
#include "AccessLogEntry.h"
#include "base/Assure.h"
#include "CacheManager.h"
#include "comm/Connection.h"
#include "errorpage.h"
#include "HttpReply.h"
#include "HttpRequest.h"
#include "icmp/net_db.h"
#include "internal.h"
#include "MemBuf.h"
#include "SquidConfig.h"
#include "Store.h"
#include "tools.h"
#include "util.h"

/* called when we "miss" on an internal object;
 * generate known dynamic objects,
 * return Http::scNotFound for others
 */
void
internalStart(const Comm::ConnectionPointer &clientConn, HttpRequest * request, StoreEntry * entry, const AccessLogEntry::Pointer &ale)
{
    ErrorState *err;

    Assure(request);
    const SBuf upath = request->url.path();
    debugs(76, 3, clientConn << " requesting '" << upath << "'");

    Assure(request->flags.internal);

    static const SBuf netdbUri("/squid-internal-dynamic/netdb");
    static const SBuf storeDigestUri("/squid-internal-periodic/store_digest");

    if (upath == netdbUri) {
        netdbBinaryExchange(entry);
    } else if (upath == storeDigestUri) {
#if USE_CACHE_DIGESTS
        const char *msgbuf = "This cache is currently building its digest.\n";
#else

        const char *msgbuf = "This cache does not support Cache Digests.\n";
#endif

        HttpReply *reply = new HttpReply;
        reply->setHeaders(Http::scNotFound, "Not Found", "text/plain", strlen(msgbuf), squid_curtime, -2);
        entry->replaceHttpReply(reply);
        entry->append(msgbuf, strlen(msgbuf));
        entry->complete();
    } else if (ForSomeCacheManager(upath)) {
        debugs(17, 2, "calling CacheManager due to URL-path");
        CacheManager::GetInstance()->start(clientConn, request, entry, ale);
    } else {
        debugObj(76, 1, "internalStart: unknown request:\n",
                 request, (ObjPackMethod) & httpRequestPack);
        err = new ErrorState(ERR_INVALID_REQ, Http::scNotFound, request, ale);
        errorAppendEntry(entry, err);
    }
}

bool
internalCheck(const SBuf &urlPath)
{
    static const SBuf InternalPfx("/squid-internal-");
    return urlPath.startsWith(InternalPfx);
}

bool
internalStaticCheck(const SBuf &urlPath)
{
    static const SBuf InternalStaticPfx("/squid-internal-static");
    return urlPath.startsWith(InternalStaticPfx);
}

bool
ForSomeCacheManager(const SBuf &urlPath)
{
    return urlPath.startsWith(CacheManager::WellKnownUrlPathPrefix());
}

/*
 * makes internal url with a given host and port (remote internal url)
 */
char *
internalRemoteUri(bool encrypt, const char *host, unsigned short port, const char *dir, const SBuf &name)
{
    static char lc_host[SQUIDHOSTNAMELEN];
    assert(host && !name.isEmpty());
    /* convert host name to lower case */
    xstrncpy(lc_host, host, SQUIDHOSTNAMELEN);
    Tolower(lc_host);

    /* check for an IP address and format appropriately if found */
    Ip::Address test = lc_host;
    if ( !test.isAnyAddr() ) {
        test.toHostStr(lc_host,SQUIDHOSTNAMELEN);
    }

    /*
     * append the domain in order to mirror the requests with appended
     * domains. If that fails, just use the hostname anyway.
     */
    (void)urlAppendDomain(lc_host);

    /* build URI */
    AnyP::Uri tmp(AnyP::PROTO_HTTP);
    tmp.host(lc_host);
    if (port)
        tmp.port(port);

    static MemBuf mb;

    mb.reset();
    mb.appendf("%s://" SQUIDSBUFPH, encrypt ? "https" : "http", SQUIDSBUFPRINT(tmp.authority()));

    if (dir)
        mb.append(dir, strlen(dir));

    mb.append(name.rawContent(), name.length());

    /* return a pointer to a local static buffer */
    return mb.buf;
}

/*
 * makes internal url with local host and port
 */
char *
internalLocalUri(const char *dir, const SBuf &name)
{
    // XXX: getMy*() may return https_port info, but we force http URIs
    // because we have not checked whether the callers can handle https.
    const bool secure = false;
    return internalRemoteUri(secure, getMyHostname(),
                             getMyPort(), dir, name);
}

const char *
internalHostname(void)
{
    LOCAL_ARRAY(char, host, SQUIDHOSTNAMELEN + 1);
    xstrncpy(host, getMyHostname(), SQUIDHOSTNAMELEN);

    /* For IPv6 addresses also check for a colon */
    if (Config.appendDomain && !strchr(host, '.') && !strchr(host, ':'))
        strncat(host, Config.appendDomain, SQUIDHOSTNAMELEN -
                strlen(host) - 1);

    Tolower(host);

    return host;
}

bool
internalHostnameIs(const SBuf &arg)
{
    if (arg.caseCmp(internalHostname()) == 0)
        return true;

    for (const auto &w : Config.hostnameAliases) {
        if (w.caseCmp(arg) == 0)
            return true;
    }

    return false;
}

