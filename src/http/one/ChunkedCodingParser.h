/*
 * Copyright (C) 1996-2015 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_HTTP_ONE_CHUNKEDCODINGPARSER_H
#define SQUID_SRC_HTTP_ONE_CHUNKEDCODINGPARSER_H

#include "http/one/forward.h"

class MemBuf;

namespace Http
{
namespace One
{

/**
 * ChunkedCodingParser is an incremental parser for chunked transfer coding
 * defined in RFC 7230 section 4.1.
 * http://tools.ietf.org/html/rfc7230#section-4.1
 *
 * The parser shovels content bytes from the raw
 * input buffer into the content output buffer, both caller-supplied.
 * Ignores chunk extensions except for ICAP's ieof.
 * Has a trailer-handling placeholder.
 */
class ChunkedCodingParser
{

public:
    ChunkedCodingParser();

    void reset();

    /**
     \retval true    complete success
     \retval false   needs more data
     \throws ??      error.
     */
    bool parse(MemBuf *rawData, MemBuf *parsedContent);

    bool needsMoreData() const;
    bool needsMoreSpace() const;

private:
    typedef void (Http1::ChunkedCodingParser::*Step)();

private:
    bool mayContinue() const;

    void parseChunkSize();
    void parseUnusedChunkExtension();
    void parseLastChunkExtension();
    void parseChunkBeg();
    void parseChunkBody();
    void parseChunkEnd();
    void parseTrailer();
    void parseTrailerHeader();
    void parseMessageEnd();

    bool findCrlf(size_t &crlfBeg, size_t &crlfEnd);
    bool findCrlf(size_t &crlfBeg, size_t &crlfEnd, bool &quoted, bool &slashed);

private:
    static Step psChunkSize;
    static Step psUnusedChunkExtension;
    static Step psLastChunkExtension;
    static Step psChunkBody;
    static Step psChunkEnd;
    static Step psTrailer;
    static Step psMessageEnd;

    MemBuf *theIn;
    MemBuf *theOut;

    Step theStep;
    uint64_t theChunkSize;
    uint64_t theLeftBodySize;
    bool doNeedMoreData;
    bool inQuoted; ///< stores parsing state for incremental findCrlf
    bool inSlashed; ///< stores parsing state for incremental findCrlf

public:
    int64_t useOriginBody;
};

} // namespace One
} // namespace Http

#endif /* SQUID_SRC_HTTP_ONE_CHUNKEDCODINGPARSER_H */

