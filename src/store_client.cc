/*
 * Copyright (C) 1996-2023 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 90    Storage Manager Client-Side Interface */

#include "squid.h"
#include "acl/FilledChecklist.h"
#include "base/AsyncCbdataCalls.h"
#include "base/CodeContext.h"
#include "event.h"
#include "globals.h"
#include "HttpReply.h"
#include "HttpRequest.h"
#include "MemBuf.h"
#include "MemObject.h"
#include "mime_header.h"
#include "sbuf/Stream.h"
#include "SquidConfig.h"
#include "SquidMath.h"
#include "StatCounters.h"
#include "Store.h"
#include "store/SwapMetaIn.h"
#include "store_swapin.h"
#include "StoreClient.h"
#if USE_DELAY_POOLS
#include "DelayPools.h"
#endif

/*
 * NOTE: 'Header' refers to the swapfile metadata header.
 *   'OBJHeader' refers to the object header, with canonical
 *   processed object headers (which may derive from FTP/HTTP etc
 *   upstream protocols
 *       'Body' refers to the swapfile body, which is the full
 *        HTTP reply (including HTTP headers and body).
 */
static StoreIOState::STRCB storeClientReadBody;
static StoreIOState::STRCB storeClientReadHeader;
static void storeClientCopy2(StoreEntry * e, store_client * sc);
static bool CheckQuickAbortIsReasonable(StoreEntry * entry);

CBDATA_CLASS_INIT(store_client);

/* StoreClient */

bool
StoreClient::onCollapsingPath() const
{
    if (!Config.onoff.collapsed_forwarding)
        return false;

    if (!Config.accessList.collapsedForwardingAccess)
        return true;

    ACLFilledChecklist checklist(Config.accessList.collapsedForwardingAccess, nullptr, nullptr);
    fillChecklist(checklist);
    return checklist.fastCheck().allowed();
}

bool
StoreClient::startCollapsingOn(const StoreEntry &e, const bool doingRevalidation) const
{
    if (!e.hittingRequiresCollapsing())
        return false; // collapsing is impossible due to the entry state

    if (!onCollapsingPath())
        return false; // collapsing is impossible due to Squid configuration

    /* collapsing is possible; the caller must collapse */

    if (const auto tags = loggingTags()) {
        if (doingRevalidation)
            tags->collapsingHistory.revalidationCollapses++;
        else
            tags->collapsingHistory.otherCollapses++;
    }

    debugs(85, 5, e << " doingRevalidation=" << doingRevalidation);
    return true;
}

/* store_client */

int
store_client::getType() const
{
    return type;
}

#if STORE_CLIENT_LIST_DEBUG
static store_client *
storeClientListSearch(const MemObject * mem, void *data)
{
    dlink_node *node;
    store_client *sc = NULL;

    for (node = mem->clients.head; node; node = node->next) {
        sc = node->data;

        if (sc->owner == data)
            return sc;
    }

    return NULL;
}

int
storeClientIsThisAClient(store_client * sc, void *someClient)
{
    return sc->owner == someClient;
}

#endif
#include "HttpRequest.h"

/* add client with fd to client list */
store_client *
storeClientListAdd(StoreEntry * e, void *data)
{
    MemObject *mem = e->mem_obj;
    store_client *sc;
    assert(mem);
#if STORE_CLIENT_LIST_DEBUG

    if (storeClientListSearch(mem, data) != NULL)
        /* XXX die! */
        assert(1 == 0);
#else
    (void)data;
#endif

    sc = new store_client (e);

    mem->addClient(sc);

    return sc;
}

StoreIOBuffer
Store::ReadBuffer::legacyOffsetBuffer(const LegacyOffset loffset) {
    assert(!Less(size(), loffset));
    return StoreIOBuffer(serialized_.size() - loffset, loffset, serialized_.data() + loffset);
}

/// finishCallback() wrapper; TODO: Add NullaryMemFunT for non-jobs.
void
store_client::FinishCallback(store_client * const sc)
{
    sc->finishCallback();
}

/// finishes a copy()-STCB sequence by synchronously calling STCB
void
store_client::finishCallback()
{
    Assure(_callback.callback_handler);
    Assure(_callback.notifier);

    StoreIOBuffer result;
    if (object_ok && parsingBuffer)
        result = parsingBuffer->packBack();
    result.flags.error = object_ok ? 0 : 1;
    result.flags.eof = copyInto.flags.eof;
    parsingBuffer.reset();

    answeredOnce = true;

    STCB *temphandler = _callback.callback_handler;
    void *cbdata = _callback.callback_data;
    _callback = Callback(nullptr, nullptr);
    copyInto.data = nullptr;

    if (cbdataReferenceValid(cbdata))
        temphandler(cbdata, result);

    cbdataReferenceDone(cbdata);
}

void
store_client::noteEof()
{
    debugs(90, 5, this);
    copyInto.flags.eof = 1;
    noteNews();
}

store_client::store_client(StoreEntry *e) :
#if STORE_CLIENT_LIST_DEBUG
    owner(cbdataReference(data)),
#endif
    entry(e),
    type(e->storeClientType()),
    object_ok(true)
{
    Assure(entry);
    entry->lock("store_client");

    flags.disk_io_pending = false;
    flags.store_copying = false;
    ++ entry->refcount;

    if (getType() == STORE_DISK_CLIENT) {
        /* assert we'll be able to get the data we want */
        /* maybe we should open swapin_sio here */
        assert(entry->hasDisk() && !entry->swapoutFailed());
    }
}

store_client::~store_client()
{
    assert(entry);
    entry->unlock("store_client");
}

/* copy bytes requested by the client */
void
storeClientCopy(store_client * sc,
                StoreEntry * e,
                StoreIOBuffer copyInto,
                STCB * callback,
                void *data)
{
    assert (sc != nullptr);
    sc->copy(e, copyInto,callback,data);
}

void
store_client::copy(StoreEntry * anEntry,
                   StoreIOBuffer copyRequest,
                   STCB * callback_fn,
                   void *data)
{
    assert (anEntry == entry);
    assert (callback_fn);
    assert (data);
    assert(!EBIT_TEST(entry->flags, ENTRY_ABORTED));
    debugs(90, 3, "store_client::copy: " << entry->getMD5Text() << ", from " <<
           copyRequest.offset << ", for length " <<
           (int) copyRequest.length << ", cb " << callback_fn << ", cbdata " <<
           data);

#if STORE_CLIENT_LIST_DEBUG

    assert(this == storeClientListSearch(entry->mem_obj, data));
#endif

    assert(!_callback.pending());
    _callback = Callback (callback_fn, cbdataReference(data));
    copyInto.data = copyRequest.data;
    copyInto.length = copyRequest.length;
    copyInto.offset = copyRequest.offset;
    Assure(copyInto.offset >= 0);

    // Our nextHttpReadOffset() expects the first copy() call to have zero
    // offset. More complex code could handle a positive first offset, but it
    // would only be useful when reading responses from memory: We would not
    // _delay_ the response (to read the requested HTTP body bytes from disk)
    // when we already can respond with HTTP headers.
    Assure(!copyInto.offset || answeredOnce);

    parsingBuffer.emplace(copyInto);

    static bool copying (false);
    assert (!copying);
    copying = true;
    /* we might be blocking comm reads due to readahead limits
     * now we have a new offset, trigger those reads...
     */
    entry->mem_obj->kickReads();
    copying = false;

    anEntry->lock("store_client::copy"); // see deletion note below

    storeClientCopy2(entry, this);

    // Bug 3480: This store_client object may be deleted now if, for example,
    // the client rejects the hit response copied above. Use on-stack pointers!

#if USE_ADAPTATION
    anEntry->kickProducer();
#endif
    anEntry->unlock("store_client::copy");

    // Add no code here. This object may no longer exist.
}

/// Whether there is (or will be) more entry data for us.
bool
store_client::moreToSend() const
{
    if (entry->store_status == STORE_PENDING)
        return true; // there may be more coming

    /* STORE_OK, including aborted entries: no more data is coming */

    // we have more to send if we have the first byte wanted by the client
    if (canReadFromMemory())
        return true;

    // If we have not sent anything, then we have to send the response headers
    // (at least). The header-less/memory-pass-through case is handled above.
    if (!answeredOnce)
        return true;

    const int64_t len = entry->objectLen();

    // If we do not know the entry length, then we have to open the swap file.
    const bool canSwapIn = entry->hasDisk();
    if (len < 0)
        return canSwapIn;

    if (copyInto.offset >= entry->contentLen())
        return false; // sent everything there is

    // if we can read more bytes from disk, we may have more bytes to send
    return canSwapIn;
}

static void
storeClientCopy2(StoreEntry * e, store_client * sc)
{
    /* reentrancy not allowed  - note this could lead to
     * dropped notifications about response data availability
     */

    if (sc->flags.store_copying) {
        debugs(90, 3, "prevented recursive copying for " << *e);
        return;
    }

    debugs(90, 3, "storeClientCopy2: " << e->getMD5Text());
    assert(sc->_callback.pending());
    /*
     * We used to check for ENTRY_ABORTED here.  But there were some
     * problems.  For example, we might have a slow client (or two) and
     * the peer server is reading far ahead and swapping to disk.  Even
     * if the peer aborts, we want to give the client(s)
     * everything we got before the abort condition occurred.
     */
    sc->doCopy(e);
}

void
store_client::doCopy(StoreEntry *anEntry)
{
    Assure(_callback.pending());
    Assure(!flags.disk_io_pending);
    Assure(!flags.store_copying);

    assert (anEntry == entry);
    flags.store_copying = true;
    MemObject *mem = entry->mem_obj;

    debugs(33, 5, this << " into " << copyInto <<
           " hi: " << mem->endOffset() <<
           " objectLen: " << entry->objectLen() <<
           " first: " << !answeredOnce);

    if (!moreToSend()) {
        /* There is no more to send! */
        debugs(33, 3, "There is no more to send!");
        noteEof();
        flags.store_copying = false;
        return;
    }

    // Send (at least) HTTP headers if we have not done so, and we have them.
    // Here, "sending HTTP headers" essentially means calling the callback when
    // the corresponding MemObject has parsed HTTP response headers.
    const auto sendHttpHeaders = !answeredOnce && mem->baseReply().hdr_sz > 0;

    // if we cannot just send HTTP headers, bail if we have no requested HTTP bytes
    if (!sendHttpHeaders && anEntry->store_status == STORE_PENDING && nextHttpReadOffset() >= mem->endOffset()) {
        debugs(90, 3, "store_client::doCopy: Waiting for more");
        flags.store_copying = false;
        return;
    }

    /*
     * Slight weirdness here.  We open a swapin file for any
     * STORE_DISK_CLIENT, even if we can copy the requested chunk
     * from memory in the next block.  We must try to open the
     * swapin file before sending any data to the client side.  If
     * we postpone the open, and then can not open the file later
     * on, the client loses big time.  Its transfer just gets cut
     * off.  Better to open it early (while the client side handler
     * is clientCacheHit) so that we can fall back to a cache miss
     * if needed.
     */

    if (STORE_DISK_CLIENT == getType() && swapin_sio == nullptr) {
        if (!startSwapin())
            return; // failure
    }

    // send any immediately available body bytes even if we also sendHttpHeaders
    if (canReadFromMemory()) {
        readFromMemory();
        noteNews(); // will sendHttpHeaders (if needed) as well
        flags.store_copying = false;
        return;
    }

    if (sendHttpHeaders) {
        debugs(33, 5, "just send headers: " << mem->baseReply().hdr_sz);
        noteNews(); // just deliver the headers
        flags.store_copying = false;
        return;
    }

    // no information that the client needs is available immediately
    scheduleDiskRead();
}

/// opens the swapin "file" if possible; otherwise, fail()s and returns false
bool
store_client::startSwapin()
{
    debugs(90, 3, "store_client::doCopy: Need to open swap in file");
    /* gotta open the swapin file */

    if (storeTooManyDiskFilesOpen()) {
        /* yuck -- this causes a TCP_SWAPFAIL_MISS on the client side */
        fail();
        flags.store_copying = false;
        return false;
    } else if (!flags.disk_io_pending) {
        /* Don't set store_io_pending here */
        storeSwapInStart(this);

        if (swapin_sio == nullptr) {
            fail();
            flags.store_copying = false;
            return false;
        }

        return true;
    } else {
        debugs(90, DBG_IMPORTANT, "WARNING: Averted multiple fd operation (1)");
        flags.store_copying = false;
        return false;
    }
}

void
store_client::noteSwapInDone(const bool error)
{
    Assure(_callback.pending());
    if (error)
        fail();
    else
        noteEof();
}

void
store_client::scheduleDiskRead()
{
    /* What the client wants is not in memory. Schedule a disk read */
    if (getType() == STORE_DISK_CLIENT) {
        // we should have called startSwapin() already
        assert(swapin_sio != nullptr);
    } else if (!swapin_sio && !startSwapin()) {
        debugs(90, 3, "bailing after swapin start failure for " << *entry);
        assert(!flags.store_copying);
        return;
    }

    assert(!flags.disk_io_pending);

    debugs(90, 3, "reading " << *entry << " from disk");

    fileRead();

    flags.store_copying = false;
}

/// whether at least one byte wanted by the client is in memory
bool
store_client::canReadFromMemory() const
{
    const auto &mem = entry->mem();
    const auto readOffset = nextHttpReadOffset();
    return mem.inmem_lo <= readOffset && readOffset < mem.endOffset();
}

/// The offset of the next stored HTTP response byte wanted by the client.
int64_t
store_client::nextHttpReadOffset() const
{
    Assure(parsingBuffer);
    const auto &mem = entry->mem();
    const auto hdr_sz = mem.baseReply().hdr_sz;
    // Certain SMP cache manager transactions do not store HTTP headers in
    // mem_hdr; they store just a kid-specific piece of the future report body.
    // In such cases, hdr_sz ought to be zero. In all other (known) cases,
    // mem_hdr contains HTTP response headers (positive hdr_sz if parsed)
    // followed by HTTP response body. This code math accommodates all cases.
    return NaturalSum<int64_t>(hdr_sz, copyInto.offset, parsingBuffer->contentSize()).value();
}

/// Copies at least some of the requested body bytes from MemObject memory,
/// satisfying the copy() request.
/// \pre canReadFromMemory() is true
void
store_client::readFromMemory()
{
    Assure(parsingBuffer);
    const auto readInto = parsingBuffer->space().positionAt(nextHttpReadOffset());

    debugs(90, 3, "copying HTTP body bytes from memory into " << readInto);
    const auto sz = entry->mem_obj->data_hdr.copy(readInto);
    Assure(sz > 0); // our canReadFromMemory() precondition guarantees that
    parsingBuffer->appended(readInto.data, sz);
}

void
store_client::fileRead()
{
    MemObject *mem = entry->mem_obj;

    assert(_callback.pending());
    assert(!flags.disk_io_pending);
    flags.disk_io_pending = true;

    // XXX: If startSwapin() is called in the middle of a copy() sequence, we
    // must ignore copyInto.offset/nextHttpReadOffset() and read from zero!
    // mem->swap_hdr_sz is zero here during initial read(s)
    const auto nextStoreReadOffset = NaturalSum<int64_t>(mem->swap_hdr_sz, nextHttpReadOffset()).value();

    // TODO: Remove this assertion. Introduced in 1998 commit 3157c72, it
    // assumes that swapped out memory is freed unconditionally, but we no
    // longer do that because trimMemory() path checks lowestMemReaderOffset().
    // It is also misplaced: We are not swapping out anything here and should
    // not care about any swapout invariants.
    if (mem->swap_hdr_sz != 0)
        if (entry->swappingOut())
            assert(mem->swapout.sio->offset() > nextStoreReadOffset);

    // XXX: We should let individual cache_dirs limit the read size instead, but
    // we cannot do that without more fixes and research because:
    // * larger reads crash code that uses SharedMemory::get() (TODO: confirm!)
    // * we do not know how to find all I/O code that assumes this limit
    // * performance effects of larger disk reads may be negative somewhere.
    const decltype(StoreIOBuffer::length) maxReadSize = SM_PAGE_SIZE;

    Assure(parsingBuffer);
    // copyInto.length bytes is the maximum we can give back to the caller
    const auto readSize = std::min(copyInto.length, maxReadSize);
    lastRead_ = parsingBuffer->makeSpace(readSize).positionAt(nextStoreReadOffset);
    debugs(90, 5, "into " << lastRead_);

    storeRead(swapin_sio,
              lastRead_.data,
              lastRead_.length,
              lastRead_.offset,
              mem->swap_hdr_sz == 0 ? storeClientReadHeader
              : storeClientReadBody,
              this);
}

void
store_client::readBody(const char * const buf, const ssize_t lastIoResult)
{
    assert(flags.disk_io_pending);
    flags.disk_io_pending = false;
    assert(_callback.pending());
    Assure(parsingBuffer);
    debugs(90, 3, "got " << lastIoResult << " using " << *parsingBuffer);

    if (lastIoResult < 0)
        return fail();

    if (!lastIoResult) {
        if (answeredOnce)
            return noteEof();

        debugs(90, DBG_CRITICAL, "ERROR: Truncated HTTP headers in on-disk object");
        return fail();
    }

    assert(lastRead_.data == buf);
    lastRead_.length = lastIoResult;

    parsingBuffer->appended(buf, lastIoResult);

    maybeWriteFromDiskToMemory(lastRead_);
    handleBodyFromDisk();
}

/// de-serializes HTTP response (partially) read from disk storage
void
store_client::handleBodyFromDisk()
{
    // We cannot de-serialize on-disk HTTP response without MemObject because
    // without MemObject::swap_hdr_sz we cannot know where that response starts.
    Assure(entry->mem_obj);
    Assure(entry->mem_obj->swap_hdr_sz > 0);

    if (!answeredOnce && entry->mem_obj->baseReply().pstate != Http::Message::psParsed) {
        if (!parseHttpHeaders())
            return;
    }

    // no callback(): we handled negative/zero lastIoResult ourselves
    noteNews();
}

/// Adds HTTP response data loaded from disk to the memory cache (if
/// needed/possible). The given part may contain portions of HTTP response
/// headers and/or HTTP response body.
void
store_client::maybeWriteFromDiskToMemory(const StoreIOBuffer &httpResponsePart)
{
    // XXX: Reject [memory-]uncachable/unshareable responses instead of assuming
    // that any HTTP response loaded from disk should be written to MemObject's
    // data_hdr (and that it may purge already cached entries). Cachability
    // decision(s) should be made outside (and obeyed by) this low-level code.
    if (httpResponsePart.length && entry->mem_obj->inmem_lo == 0 && entry->objectLen() <= (int64_t)Config.Store.maxInMemObjSize && Config.onoff.memory_cache_disk) {
        storeGetMemSpace(httpResponsePart.length);
        // XXX: The "recheck" below is not needed because nobody can purge
        // mem_hdr bytes of a locked entry, and we do lock our entry. Moreover,
        // inmem_lo offset itself should not be relevant to appending new bytes.
        // recheck for the above call may purge entry's data from the memory cache
        if (entry->mem_obj->inmem_lo == 0) {
            // XXX: This code assumes a non-shared memory cache.
            if (httpResponsePart.offset == entry->mem_obj->endOffset())
                entry->mem_obj->write(httpResponsePart);
        }
    }
}

void
store_client::fail()
{
    debugs(90, 3, (object_ok ? "once" : "again"));

    if (!object_ok)
        return; // we failed earlier; nothing to do now

    object_ok = false;

    noteNews();
}

/// if necessary and possible, informs the Store reader about copy() result
void
store_client::noteNews()
{
    /* synchronous open failures callback from the store,
     * before startSwapin detects the failure.
     * TODO: fix this inconsistent behaviour - probably by
     * having storeSwapInStart become a callback functions,
     * not synchronous
     */

    if (!_callback.callback_handler) {
        debugs(90, 5, "client lost interest");
        return;
    }

    if (_callback.notifier) {
        debugs(90, 5, "earlier news is being delivered by " << _callback.notifier);
        return;
    }

    _callback.notifier = asyncCall(90, 4, "store_client::FinishCallback", cbdataDialer(store_client::FinishCallback, this));
    ScheduleCallHere(_callback.notifier);

    Assure(!_callback.pending());
}

static void
storeClientReadHeader(void *data, const char *buf, ssize_t len, StoreIOState::Pointer)
{
    store_client *sc = (store_client *)data;
    sc->readHeader(buf, len);
}

static void
storeClientReadBody(void *data, const char *buf, ssize_t len, StoreIOState::Pointer)
{
    store_client *sc = (store_client *)data;
    sc->readBody(buf, len);
}

void
store_client::readHeader(char const *buf, ssize_t len)
{
    MemObject *const mem = entry->mem_obj;

    assert(flags.disk_io_pending);
    flags.disk_io_pending = false;
    assert(_callback.pending());

    // abort if we fail()'d earlier
    if (!object_ok)
        return;

    Assure(parsingBuffer);
    debugs(90, 3, "got " << len << " using " << *parsingBuffer);

    if (len < 0)
        return fail();

    try {
        Assure(!parsingBuffer->contentSize());
        parsingBuffer->appended(buf, len);
        Store::UnpackHitSwapMeta(buf, len, *entry);
        parsingBuffer->consume(mem->swap_hdr_sz);
    } catch (...) {
        debugs(90, DBG_IMPORTANT, "ERROR: Failed to unpack Store entry metadata: " << CurrentException);
        fail();
        return;
    }

    maybeWriteFromDiskToMemory(parsingBuffer->content());
    handleBodyFromDisk();
}

/*
 * This routine hasn't been optimised to take advantage of the
 * passed sc. Yet.
 */
int
storeUnregister(store_client * sc, StoreEntry * e, void *data)
{
    MemObject *mem = e->mem_obj;
#if STORE_CLIENT_LIST_DEBUG
    assert(sc == storeClientListSearch(e->mem_obj, data));
#else
    (void)data;
#endif

    if (mem == nullptr)
        return 0;

    debugs(90, 3, "storeUnregister: called for '" << e->getMD5Text() << "'");

    if (sc == nullptr) {
        debugs(90, 3, "storeUnregister: No matching client for '" << e->getMD5Text() << "'");
        return 0;
    }

    if (mem->clientCount() == 0) {
        debugs(90, 3, "storeUnregister: Consistency failure - store client being unregistered is not in the mem object's list for '" << e->getMD5Text() << "'");
        return 0;
    }

    dlinkDelete(&sc->node, &mem->clients);
    -- mem->nclients;

    const auto swapoutFinished = e->swappedOut() || e->swapoutFailed();
    if (e->store_status == STORE_OK && !swapoutFinished)
        e->swapOut();

    if (sc->swapin_sio != nullptr) {
        storeClose(sc->swapin_sio, StoreIOState::readerDone);
        sc->swapin_sio = nullptr;
        ++statCounter.swap.ins;
    }

    if (sc->_callback.callback_handler || sc->_callback.notifier) {
        debugs(90, 3, "forgetting store_client callback for " << *e);
        // Do not notify: Callers want to stop copying and forget about this
        // pending copy request. Some would mishandle a notification from here.
        if (sc->_callback.notifier)
            sc->_callback.notifier->cancel("storeUnregister");
    }

#if STORE_CLIENT_LIST_DEBUG
    cbdataReferenceDone(sc->owner);

#endif

    // We must lock to safely dereference e below, after deleting sc and after
    // calling CheckQuickAbortIsReasonable().
    e->lock("storeUnregister");

    // XXX: We might be inside sc store_client method somewhere up the call
    // stack. TODO: Convert store_client to AsyncJob to make destruction async.
    delete sc;

    if (CheckQuickAbortIsReasonable(e))
        e->abort();
    else
        mem->kickReads();

#if USE_ADAPTATION
    e->kickProducer();
#endif

    e->unlock("storeUnregister");
    return 1;
}

/* Call handlers waiting for  data to be appended to E. */
void
StoreEntry::invokeHandlers()
{
    if (EBIT_TEST(flags, DELAY_SENDING)) {
        debugs(90, 3, "DELAY_SENDING is on, exiting " << *this);
        return;
    }
    if (EBIT_TEST(flags, ENTRY_FWD_HDR_WAIT)) {
        debugs(90, 3, "ENTRY_FWD_HDR_WAIT is on, exiting " << *this);
        return;
    }

    /* Commit what we can to disk, if appropriate */
    swapOut();
    int i = 0;
    store_client *sc;
    dlink_node *nx = nullptr;
    dlink_node *node;

    debugs(90, 3, mem_obj->nclients << " clients; " << *this << ' ' << getMD5Text());
    /* walk the entire list looking for valid callbacks */

    const auto savedContext = CodeContext::Current();
    for (node = mem_obj->clients.head; node; node = nx) {
        sc = (store_client *)node->data;
        nx = node->next;
        ++i;

        if (!sc->_callback.pending())
            continue;

        if (sc->flags.disk_io_pending)
            continue;

        if (sc->flags.store_copying)
            continue;

        // XXX: If invokeHandlers() is (indirectly) called from a store_client
        // method, then the above three conditions may not be sufficient to
        // prevent us from reentering the same store_client object! This
        // probably does not happen in the current code, but no observed
        // invariant prevents this from (accidentally) happening in the future.

        // TODO: Convert store_client into AsyncJob; make this call asynchronous
        CodeContext::Reset(sc->_callback.codeContext);
        debugs(90, 3, "checking client #" << i);
        storeClientCopy2(this, sc);
    }
    CodeContext::Reset(savedContext);
}

// Does not account for remote readers/clients.
int
storePendingNClients(const StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    int npend = nullptr == mem ? 0 : mem->nclients;
    debugs(90, 3, "storePendingNClients: returning " << npend);
    return npend;
}

/* return true if the request should be aborted */
static bool
CheckQuickAbortIsReasonable(StoreEntry * entry)
{
    assert(entry);
    debugs(90, 3, "entry=" << *entry);

    if (storePendingNClients(entry) > 0) {
        debugs(90, 3, "quick-abort? NO storePendingNClients() > 0");
        return false;
    }

    if (Store::Root().transientReaders(*entry)) {
        debugs(90, 3, "quick-abort? NO still have one or more transient readers");
        return false;
    }

    if (entry->store_status != STORE_PENDING) {
        debugs(90, 3, "quick-abort? NO store_status != STORE_PENDING");
        return false;
    }

    if (EBIT_TEST(entry->flags, ENTRY_SPECIAL)) {
        debugs(90, 3, "quick-abort? NO ENTRY_SPECIAL");
        return false;
    }

    if (shutting_down) {
        debugs(90, 3, "quick-abort? YES avoid heavy optional work during shutdown");
        return true;
    }

    MemObject * const mem = entry->mem_obj;
    assert(mem);
    debugs(90, 3, "mem=" << mem);

    if (mem->request && !mem->request->flags.cachable) {
        debugs(90, 3, "quick-abort? YES !mem->request->flags.cachable");
        return true;
    }

    if (EBIT_TEST(entry->flags, KEY_PRIVATE)) {
        debugs(90, 3, "quick-abort? YES KEY_PRIVATE");
        return true;
    }

    const auto &reply = mem->baseReply();

    if (reply.hdr_sz <= 0) {
        // TODO: Check whether this condition works for HTTP/0 responses.
        debugs(90, 3, "quick-abort? YES no object data received yet");
        return true;
    }

    if (Config.quickAbort.min < 0) {
        debugs(90, 3, "quick-abort? NO disabled");
        return false;
    }

    if (mem->request && mem->request->range && mem->request->getRangeOffsetLimit() < 0) {
        // the admin has configured "range_offset_limit none"
        debugs(90, 3, "quick-abort? NO admin configured range replies to full-download");
        return false;
    }

    if (reply.content_length < 0) {
        // XXX: cf.data.pre does not document what should happen in this case
        // We know that quick_abort is enabled, but no limit can be applied.
        debugs(90, 3, "quick-abort? YES unknown content length");
        return true;
    }
    const auto expectlen = reply.hdr_sz + reply.content_length;

    int64_t curlen =  mem->endOffset();

    if (curlen > expectlen) {
        debugs(90, 3, "quick-abort? YES bad content length (" << curlen << " of " << expectlen << " bytes received)");
        return true;
    }

    if ((expectlen - curlen) < (Config.quickAbort.min << 10)) {
        debugs(90, 3, "quick-abort? NO only a little more object left to receive");
        return false;
    }

    if ((expectlen - curlen) > (Config.quickAbort.max << 10)) {
        debugs(90, 3, "quick-abort? YES too much left to go");
        return true;
    }

    // XXX: This is absurd! TODO: For positives, "a/(b/c) > d" is "a*c > b*d".
    if (expectlen < 100) {
        debugs(90, 3, "quick-abort? NO avoid FPE");
        return false;
    }

    if ((curlen / (expectlen / 100)) > (Config.quickAbort.pct)) {
        debugs(90, 3, "quick-abort? NO past point of no return");
        return false;
    }

    debugs(90, 3, "quick-abort? YES default");
    return true;
}

/// parses HTTP header accumulated at the beginning of copyInto
/// \returns false if fail() or scheduleDiskRead() has been called and, hence,
/// the caller should just quit without any further action
bool
store_client::parseHttpHeaders()
{
    try {
        return tryParsingHttpHeaders();
    } catch (...) {
        debugs(90, DBG_CRITICAL, "ERROR: Cannot parse on-disk HTTP headers" <<
               Debug::Extra << "exception: " << CurrentException <<
               Debug::Extra << "raw input size: " << parsingBuffer->contentSize() << " bytes" <<
               Debug::Extra << "current buffer capacity: " << parsingBuffer->capacity() << " bytes");
        fail();
        return false;
    }
}

/// parseHttpHeaders() helper
/// \copydoc parseHttpHeaders()
bool
store_client::tryParsingHttpHeaders()
{
    Assure(!copyInto.offset);

    Assure(parsingBuffer);
    const auto bufferedSize = parsingBuffer->contentSize();

    auto error = Http::scNone;
    auto &adjustableReply = entry->mem().adjustableBaseReply();
    const bool eof = false; // TODO: Remove after removing atEnd from HttpHeader::parse()
    if (adjustableReply.parse(parsingBuffer->c_str(), bufferedSize, eof, &error)) {
        debugs(90, 7, "success after accumulating " << bufferedSize << " bytes and parsing " << adjustableReply.hdr_sz);
        Assure(adjustableReply.pstate == Http::Message::psParsed);
        Assure(adjustableReply.hdr_sz > 0);
        Assure(!Less(bufferedSize, adjustableReply.hdr_sz)); // cannot parse more bytes than we have
        parsingBuffer->consume(adjustableReply.hdr_sz); // skip parsed HTTP headers

        const auto httpBodyBytesAfterHeader = parsingBuffer->contentSize();
        Assure(httpBodyBytesAfterHeader <= copyInto.length);
        debugs(90, 5, "buffered HTTP body prefix: " << httpBodyBytesAfterHeader);
        return true;
    }

    if (error)
        throw TextException(ToSBuf("malformed HTTP headers; parser error code: ", error), Here());

    // the parse() call above enforces Config.maxReplyHeaderSize limit
    // XXX: Make this a strict comparison after fixing Http::Message::parse() enforcement
    Assure(bufferedSize <= Config.maxReplyHeaderSize);

    debugs(90, 3, "need more HTTP header bytes after accumulating " << bufferedSize <<
           " out of " << Config.maxReplyHeaderSize);

    // Continue on the disk-reading path because readFromMemory() cannot give us
    // the missing header bytes: We would not be _parsing_ the header otherwise.
    scheduleDiskRead();
    return false;
}

void
store_client::dumpStats(MemBuf * output, int clientNumber) const
{
    if (_callback.pending())
        return;

    output->appendf("\tClient #%d, %p\n", clientNumber, _callback.callback_data);
    output->appendf("\t\tcopy_offset: %" PRId64 "\n", copyInto.offset);
    output->appendf("\t\tcopy_size: %" PRIuSIZE "\n", copyInto.length);
    output->append("\t\tflags:", 8);

    if (flags.disk_io_pending)
        output->append(" disk_io_pending", 16);

    if (flags.store_copying)
        output->append(" store_copying", 14);

    if (_callback.notifier)
        output->append(" notifying", 10);

    output->append("\n",1);
}

bool
store_client::Callback::pending() const
{
    return callback_handler && !notifier;
}

store_client::Callback::Callback(STCB *function, void *data):
    callback_handler(function),
    callback_data(data),
    codeContext(CodeContext::Current())
{
}

#if USE_DELAY_POOLS
int
store_client::bytesWanted() const
{
    // TODO: To avoid using stale copyInto, return zero if !_callback.pending()?
    return delayId.bytesWanted(0, copyInto.length);
}

void
store_client::setDelayId(DelayId delay_id)
{
    delayId = delay_id;
}
#endif

