/*
 * Copyright (C) 1996-2020 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID__SRC_BASE_CLPMAP_H
#define SQUID__SRC_BASE_CLPMAP_H

#include "mem/PoolingAllocator.h"
#include "sbuf/Algorithms.h"
#include "SquidTime.h"

#include <functional>
#include <list>
#include <unordered_map>

template<class EntryValue>
size_t
DefaultMemoryUsage(const EntryValue *e)
{
    return sizeof(*e);
}

/// An in-memory cache enforcing three primary policies:
/// Capacity: The memory used by cached entries has a configurable limit;
/// Lifetime: Entries are hidden (and may be deleted) after their TTL expires;
/// Priority: Capacity victims are purged in LRU order.
template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *) = DefaultMemoryUsage>
class ClpMap
{
public:
    /// maximum desired entry caching duration (a.k.a. TTL), in seconds
    using Ttl = int;

    explicit ClpMap(size_t aCapacity) { setMemLimit(aCapacity); }
    ClpMap(size_t aCapacity, Ttl aDefaultTtl);
    ~ClpMap() = default;
    ClpMap(ClpMap const &) = delete;
    ClpMap & operator = (ClpMap const &) = delete;

    /// Search for an entry, and return a pointer
    EntryValue *get(const Key &);
    /// Add an entry to the map (with the given seconds-based TTL)
    bool add(const Key &, EntryValue *, Ttl);
    /// Add an entry to the map (with the default TTL)
    bool add(const Key &key, EntryValue *t) { return add(key, t, defaultTtl); }
    /// Delete an entry from the map
    void del(const Key &);
    /// Reset the memory capacity for this map, purging if needed
    void setMemLimit(size_t newLimit);
    /// The memory capacity for the map
    size_t memLimit() const {return memLimit_;}
    /// The free space of the map
    size_t freeMem() const { return memLimit() - memoryUsed(); }
    /// The current memory usage of the map
    size_t memoryUsed() const {return memUsed_;}
    /// The number of stored entries
    size_t entries() const { return data.size(); }

private:
    /// cache entry Key, EntryValue, and caching-related entry metadata keeper
    class Entry
    {
    public:
        Entry(const Key &aKey, EntryValue *t, Ttl ttl): key(aKey), value(t), expires(squid_curtime+ttl) {}
        ~Entry() { delete value; }
        Entry(const Entry &) = delete;
        Entry & operator = (const Entry &) = delete;

        bool expired() const { return expires < squid_curtime; }

    public:
        Key key; ///< the key of entry
        EntryValue *value = nullptr; ///< A pointer to the stored value
        time_t expires = 0; ///< get() stops returning the entry after this time
        size_t memCounted = 0; ///< memory accounted for this entry in parent ClpMap
    };

    /// container for stored data
    typedef std::list<Entry, PoolingAllocator<Entry> > Storage;
    typedef typename Storage::iterator StorageIterator;

    /// Key:Entry* mapping for fast lookups by key
    typedef std::pair<Key, StorageIterator> MapItem;
    /// key:queue_item mapping for fast lookups by key
    typedef std::unordered_map<Key, StorageIterator, std::hash<Key>, std::equal_to<Key>, PoolingAllocator<MapItem> > KeyMapping;
    typedef typename KeyMapping::iterator KeyMapIterator;

    static size_t MemoryCountedFor(const Key &, const EntryValue *);

    void trim(size_t wantSpace);
    void erase(const KeyMapIterator &);
    KeyMapIterator find(const Key &);

    /// The {key, value, ttl} tuples.
    /// Currently stored and maintained in LRU sequence.
    Storage data;

    /// index of stored data by key
    KeyMapping index;

    /// seconds-based entry TTL to use if none provided to add()
    Ttl defaultTtl = std::numeric_limits<Ttl>::max();
    size_t memLimit_ = 0; ///< The maximum memory to use
    size_t memUsed_ = 0;  ///< The amount of memory currently used
};

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
ClpMap<Key, EntryValue, MemoryUsedByEV>::ClpMap(const size_t aCapacity, const Ttl aDefaultTtl):
    defaultTtl(aDefaultTtl)
{
    assert(aDefaultTtl >= 0);
    setMemLimit(aCapacity);
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
void
ClpMap<Key, EntryValue, MemoryUsedByEV>::setMemLimit(const size_t newLimit)
{
    assert(newLimit >= 0);
    if (memUsed_ > newLimit)
        trim(memLimit_ - newLimit);
    memLimit_ = newLimit;
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
typename ClpMap<Key, EntryValue, MemoryUsedByEV>::KeyMapIterator
ClpMap<Key, EntryValue, MemoryUsedByEV>::find(const Key &key)
{
    const auto i = index.find(key);
    if (i == index.end()) {
        return i;
    }

    const auto e = i->second;
    if (!e->expired()) {
        if (e != data.begin())
            data.splice(data.begin(), data, e);
        return i;
    }
    // else fall through to cleanup

    erase(i);
    return index.end();
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
EntryValue *
ClpMap<Key, EntryValue, MemoryUsedByEV>::get(const Key &key)
{
    const auto i = find(key);
    if (i != index.end()) {
        const Entry &e = *(i->second);
        return e.value;
    }
    return nullptr;
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
size_t
ClpMap<Key, EntryValue, MemoryUsedByEV>::MemoryCountedFor(const Key &k, const EntryValue *v)
{
    // approximate calculation (e.g., containers store wrappers not value_types)
    const auto storageSz = sizeof(typename Storage::value_type) + k.length() + MemoryUsedByEV(v);
    const auto indexSz = sizeof(typename KeyMapping::value_type) + k.length();
    return storageSz + indexSz;
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
bool
ClpMap<Key, EntryValue, MemoryUsedByEV>::add(const Key &key, EntryValue *t, Ttl ttl)
{
    if (memLimit() == 0)
        return false;

    del(key);

    if (ttl < 0)
        return false;

    const auto wantSpace = MemoryCountedFor(key, t);
    if (wantSpace > memLimit())
        return false;
    trim(wantSpace);

    data.emplace_front(key, t, ttl);
    index.emplace(key, data.begin());

    data.begin()->memCounted = wantSpace;
    memUsed_ += wantSpace;
    return true;
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
void
ClpMap<Key, EntryValue, MemoryUsedByEV>::erase(const KeyMapIterator &i)
{
    assert(i != index.end());
    const auto dataPosition = i->second;
    const auto sz = dataPosition->memCounted;
    index.erase(i); // destroys a pointer to our Entry
    data.erase(dataPosition); // destroys our Entry
    memUsed_ -= sz;
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
void
ClpMap<Key, EntryValue, MemoryUsedByEV>::del(const Key &key)
{
    const auto i = find(key);
    erase(i);
}

template <class Key, class EntryValue, size_t MemoryUsedByEV(const EntryValue *)>
void
ClpMap<Key, EntryValue, MemoryUsedByEV>::trim(size_t wantSpace)
{
    assert(wantSpace <= memLimit()); // no infinite loops and in-vain trimming
    while (freeMem() < wantSpace) {
        assert(!data.empty());
        // TODO: Purge expired entries first. They are useless, but their
        // presence may lead to purging potentially useful fresh entries here.
        del(data.rbegin()->key);
    }
}

#endif /* SQUID__SRC_BASE_CLPMAP_H */
