/*
 * Copyright (C) 1996-2015 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* debug section 29 */

#include "squid.h"
#include "auth/UserNameCache.h"
#include "acl/Gadgets.h" //for aclCacheMatchFlush
#include "Debug.h"
#include "event.h"
#include "SquidConfig.h"
#include "SquidTime.h"

// XXX: reset debugs levels to something sane

namespace Auth {

CBDATA_CLASS_INIT(UserNameCache);

UserNameCache::UserNameCache(const char *name) :
    cachename(name), cacheCleanupEventName("User cache cleanup: ")
{
    debugs(29, 2, "initializing " << name << " username cache");
    cacheCleanupEventName.append(name);
    eventAdd(cacheCleanupEventName.c_str(), &UserNameCache::Cleanup,
                    this, ::Config.authenticateGCInterval, 1);
    RegisterRunner(this);
}

Auth::User::Pointer
UserNameCache::lookup(const SBuf &userKey) const
{
    debugs(29, 2, "lookup for " << userKey);
    auto p = store_.find(userKey);
    if (p == store_.end())
        return User::Pointer(nullptr);
    return p->second;
}

void
UserNameCache::Cleanup(void *data)
{
    debugs(29, 2, "checkpoint");
    if (!cbdataReferenceValid(data))
        return;
    // data is this in disguise
    UserNameCache *self = static_cast<UserNameCache *>(data);
    // cache entries with expiretime <= expirationTime are to be evicted
    const time_t expirationTime =  current_time.tv_sec - ::Config.authenticateTTL;
    const auto end = self->store_.end();
    for (auto i = self->store_.begin(); i != end; ++i) {
        if (i->second->expiretime <= expirationTime) {
            debugs(29, 2, "evicting " << i->first);
            self->store_.erase(i);
        }
    }
    eventAdd(self->cacheCleanupEventName.c_str(), &UserNameCache::Cleanup,
                    self, ::Config.authenticateGCInterval, 1);
}

void
UserNameCache::insert(Auth::User::Pointer anAuth_user)
{
    debugs(29, 2, "adding " << anAuth_user->SBUserKey());
    store_[anAuth_user->SBUserKey()] = anAuth_user;
}

// generates the list of cached usernames in a format that is convenient
// to merge with equivalent lists obtained from other UserNameCaches.
std::vector<Auth::User::Pointer>
UserNameCache::sortedUsersList() const
{
    std::vector<Auth::User::Pointer> rv(size(), nullptr);
    std::transform(store_.begin(), store_.end(), rv.begin(),
        [](StoreType::value_type v) { return v.second; }
    );
    sort(rv.begin(), rv.end(),
        [](const Auth::User::Pointer &lhs, const Auth::User::Pointer &rhs) {
            return strcmp(lhs->username(), rhs->username()) < 0;
        }
    );
    return rv;
}

void
UserNameCache::endingShutdown()
{
    debugs(29, 2, "Shutting down username cache " << cachename);
    eventDelete(&UserNameCache::Cleanup, this);
    reset();
}

void
UserNameCache::syncConfig()
{
    debugs(29, 2, "Reconfiguring username cache " << cachename);
    for (auto i : store_) {
        aclCacheMatchFlush(&i.second->proxy_match_cache); //flush
    }
}

} /* namespace Auth */
