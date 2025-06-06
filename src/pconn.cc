/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 48    Persistent Connections */

#include "squid.h"
#include "base/IoManip.h"
#include "base/PackableStream.h"
#include "CachePeer.h"
#include "comm.h"
#include "comm/Connection.h"
#include "comm/Read.h"
#include "fd.h"
#include "fde.h"
#include "globals.h"
#include "mgr/Registration.h"
#include "neighbors.h"
#include "pconn.h"
#include "PeerPoolMgr.h"
#include "SquidConfig.h"
#include "Store.h"

#define PCONN_FDS_SZ    8   /* pconn set size, increase for better memcache hit rate */

//TODO: re-attach to MemPools. WAS: static Mem::Allocator *pconn_fds_pool = nullptr;
PconnModule * PconnModule::instance = nullptr;
CBDATA_CLASS_INIT(IdleConnList);

/* ========== IdleConnList ============================================ */

IdleConnList::IdleConnList(const char *aKey, PconnPool *thePool) :
    capacity_(PCONN_FDS_SZ),
    size_(0),
    parent_(thePool)
{
    //Initialize hash_link members
    key = xstrdup(aKey);
    next = nullptr;

    theList_ = new Comm::ConnectionPointer[capacity_];

    registerRunner();

// TODO: re-attach to MemPools. WAS: theList = (?? *)pconn_fds_pool->alloc();
}

IdleConnList::~IdleConnList()
{
    if (parent_)
        parent_->unlinkList(this);

    if (size_) {
        parent_ = nullptr; // prevent reentrant notifications and deletions
        closeN(size_);
    }

    delete[] theList_;

    xfree(key);
}

/** Search the list. Matches by FD socket number.
 * Performed from the end of list where newest entries are.
 *
 * \retval <0   The connection is not listed
 * \retval >=0  The connection array index
 */
int
IdleConnList::findIndexOf(const Comm::ConnectionPointer &conn) const
{
    for (auto right = size_; right > 0; --right) {
        const auto index = right - 1;
        if (conn->fd == theList_[index]->fd) {
            debugs(48, 3, "found " << conn << " at index " << index);
            return index;
        }
    }

    debugs(48, 2, conn << " NOT FOUND!");
    return -1;
}

/** Remove the entry at specified index.
 * May perform a shuffle of list entries to fill the gap.
 * \retval false The index is not an in-use entry.
 */
bool
IdleConnList::removeAt(size_t index)
{
    if (index >= size_)
        return false;
    assert(size_ > 0);

    // shuffle the remaining entries to fill the new gap.
    for (; index < size_ - 1; ++index)
        theList_[index] = theList_[index + 1];
    theList_[--size_] = nullptr;

    if (parent_) {
        parent_->noteConnectionRemoved();
        if (size_ == 0) {
            debugs(48, 3, "deleting " << hashKeyStr(this));
            delete this;
        }
    }

    return true;
}

// almost a duplicate of removeFD. But drops multiple entries.
void
IdleConnList::closeN(const size_t n)
{
    if (n < 1) {
        debugs(48, 2, "Nothing to do.");
        return;
    } else if (n >= size_) {
        debugs(48, 2, "Closing all entries.");
        while (size_ > 0) {
            const Comm::ConnectionPointer conn = theList_[--size_];
            theList_[size_] = nullptr;
            clearHandlers(conn);
            conn->close();
            if (parent_)
                parent_->noteConnectionRemoved();
        }
    } else { //if (n < size_)
        debugs(48, 2, "Closing " << n << " of " << size_ << " entries.");

        size_t index;
        // ensure the first N entries are closed
        for (index = 0; index < n; ++index) {
            const Comm::ConnectionPointer conn = theList_[index];
            theList_[index] = nullptr;
            clearHandlers(conn);
            conn->close();
            if (parent_)
                parent_->noteConnectionRemoved();
        }
        // shuffle the list N down.
        for (index = 0; index < size_ - n; ++index) {
            theList_[index] = theList_[index + n];
        }
        // ensure the last N entries are unset
        while (index < size_) {
            theList_[index] = nullptr;
            ++index;
        }
        size_ -= n;
    }

    if (parent_ && size_ == 0) {
        debugs(48, 3, "deleting " << hashKeyStr(this));
        delete this;
    }
}

void
IdleConnList::clearHandlers(const Comm::ConnectionPointer &conn)
{
    debugs(48, 3, "removing close handler for " << conn);
    comm_read_cancel(conn->fd, IdleConnList::Read, this);
    commUnsetConnTimeout(conn);
}

void
IdleConnList::push(const Comm::ConnectionPointer &conn)
{
    if (size_ == capacity_) {
        debugs(48, 3, "growing idle Connection array");
        capacity_ <<= 1;
        const Comm::ConnectionPointer *oldList = theList_;
        theList_ = new Comm::ConnectionPointer[capacity_];
        for (size_t index = 0; index < size_; ++index)
            theList_[index] = oldList[index];

        delete[] oldList;
    }

    if (parent_)
        parent_->noteConnectionAdded();

    theList_[size_] = conn;
    ++size_;
    AsyncCall::Pointer readCall = commCbCall(5,4, "IdleConnList::Read",
                                  CommIoCbPtrFun(IdleConnList::Read, this));
    comm_read(conn, fakeReadBuf_, sizeof(fakeReadBuf_), readCall);
    AsyncCall::Pointer timeoutCall = commCbCall(5,4, "IdleConnList::Timeout",
                                     CommTimeoutCbPtrFun(IdleConnList::Timeout, this));
    commSetConnTimeout(conn, conn->timeLeft(Config.Timeout.serverIdlePconn), timeoutCall);
}

/// Determine whether an entry in the idle list is available for use.
/// Returns false if the entry is unset, closed or closing.
bool
IdleConnList::isAvailable(int i) const
{
    const Comm::ConnectionPointer &conn = theList_[i];

    // connection already closed. useless.
    if (!Comm::IsConnOpen(conn))
        return false;

    // our connection early-read/close handler is scheduled to run already. unsafe
    if (!COMMIO_FD_READCB(conn->fd)->active())
        return false;

    return true;
}

Comm::ConnectionPointer
IdleConnList::pop()
{
    for (auto right = size_; right > 0; --right) {
        const auto i = right - 1;

        if (!isAvailable(i))
            continue;

        // our connection timeout handler is scheduled to run already. unsafe for now.
        // TODO: cancel the pending timeout callback and allow re-use of the conn.
        if (fd_table[theList_[i]->fd].timeoutHandler == nullptr)
            continue;

        // the cache_peer has been removed from the configuration
        // TODO: remove all such connections at once during reconfiguration
        if (theList_[i]->toGoneCachePeer())
            continue;

        // finally, a match. pop and return it.
        Comm::ConnectionPointer result = theList_[i];
        clearHandlers(result);
        /* may delete this */
        removeAt(i);
        return result;
    }

    return Comm::ConnectionPointer();
}

/*
 * XXX this routine isn't terribly efficient - if there's a pending
 * read event (which signifies the fd will close in the next IO loop!)
 * we ignore the FD and move onto the next one. This means, as an example,
 * if we have a lot of FDs open to a very popular server and we get a bunch
 * of requests JUST as they timeout (say, it shuts down) we'll be wasting
 * quite a bit of CPU. Just keep it in mind.
 */
Comm::ConnectionPointer
IdleConnList::findUseable(const Comm::ConnectionPointer &aKey)
{
    assert(size_);

    // small optimization: do the constant bool tests only once.
    const bool keyCheckAddr = !aKey->local.isAnyAddr();
    const bool keyCheckPort = aKey->local.port() > 0;

    for (auto right = size_; right > 0; --right) {
        const auto i = right - 1;

        if (!isAvailable(i))
            continue;

        // local end port is required, but do not match.
        if (keyCheckPort && aKey->local.port() != theList_[i]->local.port())
            continue;

        // local address is required, but does not match.
        if (keyCheckAddr && aKey->local.matchIPAddr(theList_[i]->local) != 0)
            continue;

        // our connection timeout handler is scheduled to run already. unsafe for now.
        // TODO: cancel the pending timeout callback and allow re-use of the conn.
        if (fd_table[theList_[i]->fd].timeoutHandler == nullptr)
            continue;

        // the cache_peer has been removed from the configuration
        // TODO: remove all such connections at once during reconfiguration
        if (theList_[i]->toGoneCachePeer())
            continue;

        // finally, a match. pop and return it.
        Comm::ConnectionPointer result = theList_[i];
        clearHandlers(result);
        /* may delete this */
        removeAt(i);
        return result;
    }

    return Comm::ConnectionPointer();
}

/* might delete list */
void
IdleConnList::findAndClose(const Comm::ConnectionPointer &conn)
{
    const int index = findIndexOf(conn);
    if (index >= 0) {
        if (parent_)
            parent_->notifyManager("idle conn closure");
        clearHandlers(conn);
        /* might delete this */
        removeAt(index);
        conn->close();
    }
}

void
IdleConnList::Read(const Comm::ConnectionPointer &conn, char *, size_t len, Comm::Flag flag, int, void *data)
{
    debugs(48, 3, len << " bytes from " << conn);

    if (flag == Comm::ERR_CLOSING) {
        debugs(48, 3, "Comm::ERR_CLOSING from " << conn);
        /* Bail out on Comm::ERR_CLOSING - may happen when shutdown aborts our idle FD */
        return;
    }

    IdleConnList *list = (IdleConnList *) data;
    /* may delete list/data */
    list->findAndClose(conn);
}

void
IdleConnList::Timeout(const CommTimeoutCbParams &io)
{
    debugs(48, 3, io.conn);
    IdleConnList *list = static_cast<IdleConnList *>(io.data);
    /* may delete list/data */
    list->findAndClose(io.conn);
}

void
IdleConnList::endingShutdown()
{
    closeN(size_);
}

/* ========== PconnPool PRIVATE FUNCTIONS ============================================ */

const char *
PconnPool::key(const Comm::ConnectionPointer &destLink, const char *domain)
{
    LOCAL_ARRAY(char, buf, SQUIDHOSTNAMELEN * 3 + 10);

    destLink->remote.toUrl(buf, SQUIDHOSTNAMELEN * 3 + 10);

    // when connecting through a cache_peer, ignore the final destination
    if (destLink->getPeer())
        domain = nullptr;

    if (domain) {
        const int used = strlen(buf);
        snprintf(buf+used, SQUIDHOSTNAMELEN * 3 + 10-used, "/%s", domain);
    }

    debugs(48,6,"PconnPool::key(" << destLink << ", " << (domain?domain:"[no domain]") << ") is {" << buf << "}" );
    return buf;
}

void
PconnPool::dumpHist(std::ostream &yaml) const
{
    AtMostOnce heading(
        "  connection use histogram:\n"
        "    # requests per connection: closed connections that carried that many requests\n");

    for (int i = 0; i < PCONN_HIST_SZ; ++i) {
        if (hist[i] == 0)
            continue;

        yaml << heading <<
             "    " << i << ": " << hist[i] << "\n";
    }
}

void
PconnPool::dumpHash(std::ostream &yaml) const
{
    const auto hid = table;
    hash_first(hid);
    AtMostOnce title("  open connections list:\n");
    for (auto *walker = hash_next(hid); walker; walker = hash_next(hid)) {
        yaml << title <<
             "    \"" << static_cast<char *>(walker->key) << "\": " <<
             static_cast<IdleConnList *>(walker)->count() <<
             "\n";
    }
}

void
PconnPool::dump(std::ostream &yaml) const
{
    yaml << "pool " << descr << ":\n";
    dumpHist(yaml);
    dumpHash(yaml);
}

/* ========== PconnPool PUBLIC FUNCTIONS ============================================ */

PconnPool::PconnPool(const char *aDescr, const CbcPointer<PeerPoolMgr> &aMgr):
    table(nullptr), descr(aDescr),
    mgr(aMgr),
    theCount(0)
{
    int i;
    table = hash_create((HASHCMP *) strcmp, 229, hash_string);

    for (i = 0; i < PCONN_HIST_SZ; ++i)
        hist[i] = 0;

    PconnModule::GetInstance()->add(this);
}

static void
DeleteIdleConnList(void *hashItem)
{
    delete static_cast<IdleConnList*>(hashItem);
}

PconnPool::~PconnPool()
{
    PconnModule::GetInstance()->remove(this);
    hashFreeItems(table, &DeleteIdleConnList);
    hashFreeMemory(table);
    descr = nullptr;
}

void
PconnPool::push(const Comm::ConnectionPointer &conn, const char *domain)
{
    if (fdUsageHigh()) {
        debugs(48, 3, "Not many unused FDs");
        conn->close();
        return;
    } else if (shutting_down) {
        conn->close();
        debugs(48, 3, "Squid is shutting down. Refusing to do anything");
        return;
    }
    // TODO: also close used pconns if we exceed peer max-conn limit

    const char *aKey = key(conn, domain);
    IdleConnList *list = (IdleConnList *) hash_lookup(table, aKey);

    if (list == nullptr) {
        list = new IdleConnList(aKey, this);
        debugs(48, 3, "new IdleConnList for {" << hashKeyStr(list) << "}" );
        hash_join(table, list);
    } else {
        debugs(48, 3, "found IdleConnList for {" << hashKeyStr(list) << "}" );
    }

    list->push(conn);
    assert(!comm_has_incomplete_write(conn->fd));

    LOCAL_ARRAY(char, desc, FD_DESC_SZ);
    snprintf(desc, FD_DESC_SZ, "Idle server: %s", aKey);
    fd_note(conn->fd, desc);
    debugs(48, 3, "pushed " << conn << " for " << aKey);

    // successful push notifications resume multi-connection opening sequence
    notifyManager("push");
}

Comm::ConnectionPointer
PconnPool::pop(const Comm::ConnectionPointer &dest, const char *domain, bool keepOpen)
{
    // always call shared pool first because we need to close an idle
    // connection there if we have to use a standby connection.
    if (const auto direct = popStored(dest, domain, keepOpen))
        return direct;

    // either there was no pconn to pop or this is not a retriable xaction
    if (const auto peer = dest->getPeer()) {
        if (peer->standby.pool)
            return peer->standby.pool->popStored(dest, domain, true);
    }

    return nullptr;
}

/// implements pop() API while disregarding peer standby pools
/// \returns an open connection or nil
Comm::ConnectionPointer
PconnPool::popStored(const Comm::ConnectionPointer &dest, const char *domain, const bool keepOpen)
{
    const char * aKey = key(dest, domain);

    IdleConnList *list = (IdleConnList *)hash_lookup(table, aKey);
    if (list == nullptr) {
        debugs(48, 3, "lookup for key {" << aKey << "} failed.");
        // failure notifications resume standby conn creation after fdUsageHigh
        notifyManager("pop lookup failure");
        return Comm::ConnectionPointer();
    } else {
        debugs(48, 3, "found " << hashKeyStr(list) <<
               (keepOpen ? " to use" : " to kill"));
    }

    if (const auto popped = list->findUseable(dest)) { // may delete list
        // successful pop notifications replenish standby connections pool
        notifyManager("pop");

        if (keepOpen)
            return popped;

        popped->close();
        return Comm::ConnectionPointer();
    }

    // failure notifications resume standby conn creation after fdUsageHigh
    notifyManager("pop usability failure");
    return Comm::ConnectionPointer();
}

void
PconnPool::notifyManager(const char *reason)
{
    if (mgr.valid())
        PeerPoolMgr::Checkpoint(mgr, reason);
}

void
PconnPool::closeN(int n)
{
    hash_table *hid = table;
    hash_first(hid);

    // close N connections, one per list, to treat all lists "fairly"
    for (int i = 0; i < n && count(); ++i) {

        hash_link *current = hash_next(hid);
        if (!current) {
            hash_first(hid);
            current = hash_next(hid);
            Must(current); // must have one because the count() was positive
        }

        // may delete current
        static_cast<IdleConnList*>(current)->closeN(1);
    }
}

void
PconnPool::unlinkList(IdleConnList *list)
{
    theCount -= list->count();
    assert(theCount >= 0);
    hash_remove_link(table, list);
}

void
PconnPool::noteUses(int uses)
{
    if (uses >= PCONN_HIST_SZ)
        uses = PCONN_HIST_SZ - 1;

    ++hist[uses];
}

/* ========== PconnModule ============================================ */

/*
 * This simple class exists only for the cache manager
 */

PconnModule::PconnModule(): pools()
{
    registerWithCacheManager();
}

PconnModule *
PconnModule::GetInstance()
{
    if (instance == nullptr)
        instance = new PconnModule;

    return instance;
}

void
PconnModule::registerWithCacheManager(void)
{
    Mgr::RegisterAction("pconn",
                        "Persistent Connection Utilization Histograms",
                        DumpWrapper, Mgr::Protected::no, Mgr::Atomic::yes,
                        Mgr::Format::yaml);
}

void
PconnModule::add(PconnPool *aPool)
{
    pools.insert(aPool);
}

void
PconnModule::remove(PconnPool *aPool)
{
    pools.erase(aPool);
}

void
PconnModule::dump(std::ostream &yaml)
{
    for (const auto &p: pools)
        p->dump(yaml);
}

void
PconnModule::DumpWrapper(StoreEntry *e)
{
    PackableStream yaml(*e);
    PconnModule::GetInstance()->dump(yaml);
}

