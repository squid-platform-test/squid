/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 17    Request Forwarding */

#include "squid.h"
#include "base/AsyncFunCalls.h"
#include "CollapsedForwarding.h"
#include "globals.h"
#include "ipc/mem/Segment.h"
#include "ipc/Messages.h"
#include "ipc/Port.h"
#include "ipc/TypedMsgHdr.h"
#include "MemObject.h"
#include "SquidConfig.h"
#include "Store.h"
#include "store_key_md5.h"
#include "tools.h"

/// shared memory segment path to use for CollapsedForwarding queue
static const char *const ShmLabel = "cf";
/// a single worker-to-worker queue capacity
// TODO: make configurable or compute from squid.conf settings if possible
static const int QueueCapacity = 1024;

std::unique_ptr<CollapsedForwarding::Queue> CollapsedForwarding::queue;

/// IPC queue message
class CollapsedForwardingMsg
{
public:
    CollapsedForwardingMsg(): sender(-1), xitIndex(-1) {}

    /// prints message parameters; suitable for cache manager reports
    void stat(std::ostream &);

public:
    int sender; ///< kid ID of sending process

    /// transients index, so that workers can find [private] entries to sync
    sfileno xitIndex;
};

void
CollapsedForwardingMsg::stat(std::ostream &os)
{
    os << "sender: " << sender << ", xitIndex: " << xitIndex;
}

// CollapsedForwarding

void
CollapsedForwarding::Init()
{
    Must(!queue.get());
    if (UsingSmp() && IamWorkerProcess()) {
        queue.reset(new Queue(ShmLabel, KidIdentifier));
        AsyncCall::Pointer callback = asyncCall(17, 4, "CollapsedForwarding::HandleNewDataAtStart",
                                                NullaryFunDialer(&CollapsedForwarding::HandleNewDataAtStart));
        ScheduleCallHere(callback);
    }
}

void
CollapsedForwarding::Broadcast(const StoreEntry &e, const bool includingThisWorker)
{
    if (!e.hasTransients() ||
            !Store::Root().transientReaders(e)) {
        debugs(17, 7, "nobody reads " << e);
        return;
    }

    debugs(17, 5, e);
    Broadcast(e.mem_obj->xitTable.index, includingThisWorker);
}

void
CollapsedForwarding::Broadcast(const sfileno index, const bool includingThisWorker)
{
    if (!queue.get())
        return;

    CollapsedForwardingMsg msg;
    msg.sender = KidIdentifier;
    msg.xitIndex = index;

    debugs(17, 7, "entry " << index << " to " << Config.workers << (includingThisWorker ? "" : "-1") << " workers");

    // TODO: send only to workers who are waiting for data
    for (int workerId = 1; workerId <= Config.workers; ++workerId) {
        try {
            if ((workerId != KidIdentifier || includingThisWorker) && queue->push(workerId, msg))
                Notify(workerId);
        } catch (const Queue::Full &) {
            debugs(17, DBG_IMPORTANT, "ERROR: Collapsed forwarding " <<
                   "queue overflow for kid" << workerId <<
                   " at " << queue->outSize(workerId) << " items");
            // TODO: grow queue size
        }
    }
}

void
CollapsedForwarding::Notify(const int workerId)
{
    // TODO: Count and report the total number of notifications, pops, pushes.
    debugs(17, 7, "to kid" << workerId);
    Ipc::TypedMsgHdr msg;
    msg.setType(Ipc::mtCollapsedForwardingNotification);
    msg.putInt(KidIdentifier);
    const String addr = Ipc::Port::MakeAddr(Ipc::strandAddrLabel, workerId);
    Ipc::SendMessage(addr, msg);
}

void
CollapsedForwarding::HandleNewData(const char *const when)
{
    debugs(17, 4, "popping all " << when);
    CollapsedForwardingMsg msg;
    int workerId;
    int poppedCount = 0;
    while (queue->pop(workerId, msg)) {
        debugs(17, 3, "message from kid" << workerId);
        if (workerId != msg.sender) {
            debugs(17, DBG_IMPORTANT, "mismatching kid IDs: " << workerId <<
                   " != " << msg.sender);
        }

        debugs(17, 7, "handling entry " << msg.xitIndex << " in transients_map");
        Store::Root().syncCollapsed(msg.xitIndex);
        debugs(17, 7, "handled entry " << msg.xitIndex << " in transients_map");

        // XXX: stop and schedule an async call to continue
        ++poppedCount;
        assert(poppedCount < SQUID_MAXFD);
    }
}

void
CollapsedForwarding::HandleNotification(const Ipc::TypedMsgHdr &msg)
{
    const int from = msg.getInt();
    debugs(17, 7, "from " << from);
    assert(queue.get());
    queue->clearReaderSignal(from);
    HandleNewData("after notification");
}

/// Handle queued IPC messages for the first time in this process lifetime, when
/// the queue may be reflecting the state of our killed predecessor.
void
CollapsedForwarding::HandleNewDataAtStart()
{
    /// \sa IpcIoFile::HandleMessagesAtStart() -- duplicates this logic
    queue->clearAllReaderSignals();
    HandleNewData("at start");
}

void
CollapsedForwarding::StatQueue(std::ostream &os)
{
    if (queue.get()) {
        os << "Transients queues:\n";
        queue->stat<CollapsedForwardingMsg>(os);
    }
}

/// initializes shared queue used by CollapsedForwarding
class CollapsedForwardingRr: public Ipc::Mem::RegisteredRunner
{
public:
    /* RegisteredRunner API */
    CollapsedForwardingRr(): owner(nullptr) {}
    ~CollapsedForwardingRr() override;

protected:
    void create() override;
    void open() override;

private:
    Ipc::MultiQueue::Owner *owner;
};

DefineRunnerRegistrator(CollapsedForwardingRr);

void CollapsedForwardingRr::create()
{
    Must(!owner);
    owner = Ipc::MultiQueue::Init(ShmLabel, Config.workers, 1,
                                  sizeof(CollapsedForwardingMsg),
                                  QueueCapacity);
}

void CollapsedForwardingRr::open()
{
    CollapsedForwarding::Init();
}

CollapsedForwardingRr::~CollapsedForwardingRr()
{
    delete owner;
}

