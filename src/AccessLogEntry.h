/*
 * Copyright (C) 1996-2020 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_HTTPACCESSLOGENTRY_H
#define SQUID_HTTPACCESSLOGENTRY_H

#include "anyp/PortCfg.h"
#include "base/CodeContext.h"
#include "comm/Connection.h"
#include "HierarchyLogEntry.h"
#include "http/ProtocolVersion.h"
#include "http/RequestMethod.h"
#include "HttpHeader.h"
#include "icp_opcode.h"
#include "ip/Address.h"
#include "LogTags.h"
#include "MessageSizes.h"
#include "Notes.h"
#include "proxyp/forward.h"
#include "sbuf/SBuf.h"
#if ICAP_CLIENT
#include "adaptation/icap/Elements.h"
#endif
#if USE_OPENSSL
#include "ssl/gadgets.h"
#include "ssl/support.h"
#endif

/* forward decls */
class HttpReply;
class HttpRequest;
class CustomLog;

/// \brief Log info details for HTTP protocol
class AccessLogEntryHttpDetails {
public:
    HttpRequestMethod method;
    int code = 0;
    const char *content_type = nullptr;
    char *rawRequestHeaders = nullptr; //< virgin HTTP request headers
    char *adaptedRequestHeaders = nullptr; //< HTTP request headers after adaptation and redirection
    AnyP::ProtocolVersion version;

    /// counters for the original request received from client
    // TODO calculate header and payload better (by parser)
    // XXX payload encoding overheads not calculated at all yet.
    MessageSizes clientRequestSz;

    /// counters for the response sent to client
    // TODO calculate header and payload better (by parser)
    // XXX payload encoding overheads not calculated at all yet.
    MessageSizes clientReplySz;

    ~AccessLogEntryHttpDetails() {
        safe_free(rawRequestHeaders);
        safe_free(adaptedRequestHeaders);
    }
};

/// \brief Log info details for ICP protocol
class AccessLogEntryIcpDetails
{
public:
    icp_opcode opcode = ICP_INVALID;
};

/// \brief Log info details for HTCP protocol
class AccessLogEntryHtcpDetails
{
public:
    const char *opcode = nullptr;
};

/// \brief Log info details for the SSL protocol
class AccessLogEntrySslDetails
{
public:
#if USE_OPENSSL
    // const char *user = nullptr;    ///< emailAddress from the SSL client certificate
    int bumpMode = ::Ssl::bumpEnd; ///< whether and how the request was SslBumped
    Security::CertPointer sslClientCert; ///< cert received from the client
    SBuf ssluser;
#endif
};

/// \brief Log info details for the ICAP part of request
/// TODO: Rename class to something more sensible
class AccessLogEntryIcapDetails
{
#if ICAP_CLIENT
public:
    ~AccessLogEntryIcapDetails();
    Ip::Address hostAddr; ///< ICAP server IP address
    String serviceName;   ///< ICAP service name
    String reqUri;   ///< ICAP Request-URI
    Adaptation::Icap::ICAP::Method reqMethod = Adaptation::methodNone; ///< ICAP request method
    int64_t bytesSent = 0;   ///< number of bytes sent to ICAP server so far
    int64_t bytesRead = 0;   ///< number of bytes read from ICAP server so far
    /**
     * number of ICAP body bytes read from ICAP server or -1 for no encapsulated
     * message data in ICAP reply (eg 204 responses)
     */
    int64_t bodyBytesRead = -1;
    HttpRequest *request = nullptr; ///< ICAP request
    HttpReply *reply = nullptr;     ///< ICAP reply

    Adaptation::Icap::XactOutcome outcome = Adaptation::Icap::xoUnknown; ///< final transaction status
    /** \brief Transaction response time.
     * The timer starts when the ICAP transaction
     * is created and stops when the result of the transaction is logged
     */
    struct timeval trTime = {};
    /** \brief Transaction I/O time.
     * The timer starts when the first ICAP request
     * byte is scheduled for sending and stops when the lastbyte of the
     * ICAP response is received.
     */
    struct timeval ioTime = {};
    Http::StatusCode resStatus = Http::scNone; ///< ICAP response status code
    struct timeval processingTime = {};        ///< total ICAP processing time
#endif
};

/// \brief This subclass holds general adaptation log info.
class AccessLogEntryAdaptationDetails
{
#if USE_ADAPTATION
public:
    /// image of the last ICAP response header or eCAP meta received
    char *last_meta = nullptr;
    ~AccessLogEntryAdaptationDetails() {
        safe_free(last_meta);
    }
#endif
};

/// \brief This subclass holds log info for Squid internal stats
/// TODO: some details relevant to particular protocols need shuffling to other sub-classes
class AccessLogEntrySquidDetails
{
public:
    AccessLogEntrySquidDetails()
    {
        caddr.setNoAddr();
    }

    Ip::Address caddr;
    int64_t highOffset = 0;
    int64_t objectSize = 0;
    LogTags code;
    struct timeval start_time = {}; ///< The time the master transaction started
    struct timeval trTime = {};     ///< The response time
    const char *rfc931 = nullptr;
    const char *extuser = nullptr;
    AnyP::PortCfgPointer port;
};

class AccessLogEntry : public CodeContext
{

public:
    typedef RefCount<AccessLogEntry> Pointer;

    AccessLogEntry();
    virtual ~AccessLogEntry();

    /* CodeContext API */
    virtual std::ostream &detailCodeContext(std::ostream &os) const override;
    virtual ScopedId codeContextGist() const override;

    /// Fetch the client IP log string into the given buffer.
    /// Knows about several alternate locations of the IP
    /// including indirect forwarded-for IP if configured to log that
    void getLogClientIp(char *buf, size_t bufsz) const;

    /// Fetch the client IDENT string, or nil if none is available.
    const char *getClientIdent() const;

    /// Fetch the external ACL provided 'user=' string, or nil if none is available.
    const char *getExtUser() const;

    /// whether we know what the request method is
    bool hasLogMethod() const { return icp.opcode || htcp.opcode || http.method; }

    /// Fetch the transaction method string (ICP opcode, HTCP opcode or HTTP method)
    SBuf getLogMethod() const;

    void syncNotes(HttpRequest *request);

    /// dump all reply headers (for sending or risky logging)
    void packReplyHeaders(MemBuf &mb) const;

    SBuf url;

    /// TCP/IP level details about the client connection
    Comm::ConnectionPointer tcpClient;
    // TCP/IP level details about the server or peer connection
    // are stored in hier.tcpServer

    // TODO: details of HTTP held in the parent class need moving into here.
    AccessLogEntryHttpDetails http;

    AccessLogEntryIcpDetails icp;

    AccessLogEntryHtcpDetails htcp;

    AccessLogEntrySslDetails ssl;

    // TODO: this object field need renaming to 'squid' or something.
    AccessLogEntrySquidDetails cache;

    AccessLogEntryAdaptationDetails adapt;

    const char *lastAclName = nullptr; ///< string for external_acl_type %ACL format code
    SBuf lastAclData; ///< string for external_acl_type %DATA format code

    HierarchyLogEntry hier;
    HttpReplyPointer reply;
    HttpRequest *request = nullptr; //< virgin HTTP request
    HttpRequest *adapted_request = nullptr; //< HTTP request after adaptation and redirection

    /// key:value pairs set by squid.conf note directive and
    /// key=value pairs returned from URL rewrite/redirect helper
    NotePairs::Pointer notes;

    /// see ConnStateData::proxyProtocolHeader_
    ProxyProtocol::HeaderPointer proxyProtocolHeader;

    AccessLogEntryIcapDetails icap;

    /// Effective URI of the received client (or equivalent) HTTP request or,
    /// in rare cases where that information was not collected, a nil pointer.
    /// Receiving errors are represented by "error:..." URIs.
    /// Adaptations and redirections do not affect this URI.
    const SBuf *effectiveVirginUrl() const;

    /// Remember Client URI (or equivalent) when there is no HttpRequest.
    void setVirginUrlForMissingRequest(const SBuf &vu)
    {
        if (!request)
            virginUrlForMissingRequest_ = vu;
    }

private:
    /// Client URI (or equivalent) for effectiveVirginUrl() when HttpRequest is
    /// missing. This member is ignored unless the request member is nil.
    SBuf virginUrlForMissingRequest_;
};

class ACLChecklist;

/* Should be in 'AccessLog.h' as the driver */
void accessLogLogTo(CustomLog* log, AccessLogEntry::Pointer &al, ACLChecklist* checklist = NULL);
void accessLogLog(AccessLogEntry::Pointer &, ACLChecklist * checklist);
void accessLogRotate(void);
void accessLogClose(void);
void accessLogInit(void);
const char *accessLogTime(time_t);

#endif /* SQUID_HTTPACCESSLOGENTRY_H */

