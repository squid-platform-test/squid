/*
 * Copyright (C) 1996-2022 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_DOWNLOADER_H
#define SQUID_DOWNLOADER_H

#include "base/AsyncCallbacks.h"
#include "base/AsyncJob.h"
#include "defines.h"
#include "http/forward.h"
#include "http/StatusCode.h"
#include "sbuf/SBuf.h"

class ClientHttpRequest;
class StoreIOBuffer;
class clientStreamNode;
class DownloaderContext;
typedef RefCount<DownloaderContext> DownloaderContextPointer;
class MasterXaction;
using MasterXactionPointer = RefCount<MasterXaction>;

/// download result
class DownloaderAnswer {
public:
    // The body of a successfully received HTTP 200 OK reply to our GET request.
    // Unused unless outcome is Http::scOkay.
    SBuf resource;

    /// Download result summary.
    /// May differ from the status code of the downloaded HTTP reply.
    Http::StatusCode outcome = Http::scNone;
};

std::ostream &operator <<(std::ostream &os, const DownloaderAnswer &);

/// The Downloader class fetches SBuf-storable things for other Squid
/// components/transactions using internal requests. For example, it is used
/// to fetch missing intermediate certificates when validating origin server
/// certificate chains.
class Downloader: virtual public AsyncJob
{
    CBDATA_CLASS(Downloader);
public:
    using Answer = DownloaderAnswer;

    Downloader(const SBuf &url, const MasterXactionPointer &, unsigned int level = 0);
    virtual ~Downloader();
    virtual void swanSong();

    /// delays destruction to protect doCallouts()
    void downloadFinished();

    /// The nested level of Downloader object (downloads inside downloads).
    unsigned int nestedLevel() const {return level_;}

    void handleReply(clientStreamNode *, ClientHttpRequest *, HttpReply *, StoreIOBuffer);

    /// answer destination
    AsyncCallback<Answer> callback;

protected:

    /* AsyncJob API */
    virtual bool doneAll() const;
    virtual void start();

private:

    bool buildRequest();
    void callBack(Http::StatusCode const status);

    /// The maximum allowed object size.
    static const size_t MaxObjectSize = 1*1024*1024;

    SBuf url_; ///< the url to download
    SBuf object_; ///< the object body data
    const unsigned int level_; ///< holds the nested downloads level
    MasterXactionPointer masterXaction_; ///< download transaction context

    /// Pointer to an object that stores the clientStream required info
    DownloaderContextPointer context_;
};

#endif

