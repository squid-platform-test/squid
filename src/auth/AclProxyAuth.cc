/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

/* DEBUG: section 28    Access Control */

#include "squid.h"
#include "acl/FilledChecklist.h"
#include "acl/RegexData.h"
#include "acl/UserData.h"
#include "auth/Acl.h"
#include "auth/AclProxyAuth.h"
#include "auth/Gadgets.h"
#include "auth/User.h"
#include "auth/UserRequest.h"
#include "client_side.h"
#include "http/Stream.h"
#include "HttpRequest.h"

ACLProxyAuth::~ACLProxyAuth()
{
    delete data;
}

ACLProxyAuth::ACLProxyAuth(ACLData<char const *> *newData, char const *theType) :
    data(newData),
    type_(theType)
{}

char const *
ACLProxyAuth::typeString() const
{
    return type_;
}

const Acl::Options &
ACLProxyAuth::lineOptions()
{
    return data->lineOptions();
}

void
ACLProxyAuth::parse()
{
    data->parse();
}

int
ACLProxyAuth::match(ACLChecklist *checklist)
{
    auto answer = AuthenticateAcl(checklist, *this);

    // convert to tri-state ACL match 1,0,-1
    switch (answer) {
    case ACCESS_ALLOWED:
        // check for a match
        return matchProxyAuth(checklist);

    case ACCESS_DENIED:
        return 0; // non-match

    case ACCESS_DUNNO:
    case ACCESS_AUTH_REQUIRED:
    default:
        // If the answer is not allowed or denied (matches/not matches) and
        // async authentication is not in progress, then we are done.
        if (checklist->keepMatching())
            checklist->markFinished(answer, "AuthenticateAcl exception");
        return -1; // other
    }
}

SBufList
ACLProxyAuth::dump() const
{
    return data->dump();
}

bool
ACLProxyAuth::empty() const
{
    return data->empty();
}

bool
ACLProxyAuth::valid() const
{
    if (authenticateSchemeCount() == 0) {
        debugs(28, DBG_CRITICAL, "ERROR: Cannot use proxy auth because no authentication schemes were compiled.");
        return false;
    }

    if (authenticateActiveSchemeCount() == 0) {
        debugs(28, DBG_CRITICAL, "ERROR: Cannot use proxy auth because no authentication schemes are fully configured.");
        return false;
    }

    return true;
}

void
ACLProxyAuth::StartLookup(ACLFilledChecklist &cl, const Acl::Node &)
{
    debugs(28, 3, "checking password via authenticator");

    /* make sure someone created auth_user_request for us */
    assert(cl.auth_user_request != nullptr);
    assert(cl.auth_user_request->valid());
    cl.auth_user_request->start(cl.request.getRaw(), cl.al, LookupDone, &cl);
}

void
ACLProxyAuth::LookupDone(void *data)
{
    ACLFilledChecklist *checklist = Filled(static_cast<ACLChecklist*>(data));

    if (checklist->auth_user_request == nullptr || !checklist->auth_user_request->valid() || checklist->conn() == nullptr) {
        /* credentials could not be checked either way
         * restart the whole process */
        /* OR the connection was closed, there's no way to continue */
        checklist->auth_user_request = nullptr;

        if (checklist->conn() != nullptr) {
            checklist->conn()->setAuth(nullptr, "proxy_auth ACL failure");
        }
    }

    checklist->resumeNonBlockingCheck();
}

int
ACLProxyAuth::matchForCache(ACLChecklist *cl)
{
    ACLFilledChecklist *checklist = Filled(cl);
    assert (checklist->auth_user_request != nullptr);
    return data->match(checklist->auth_user_request->username());
}

/* aclMatchProxyAuth can return two exit codes:
 * 0 : Authorisation for this ACL failed. (Did not match)
 * 1 : Authorisation OK. (Matched)
 */
int
ACLProxyAuth::matchProxyAuth(ACLChecklist *cl)
{
    ACLFilledChecklist *checklist = Filled(cl);
    if (!checklist->request->flags.sslBumped) {
        if (!authenticateUserAuthenticated(checklist->auth_user_request)) {
            return 0;
        }
    }
    /* check to see if we have matched the user-acl before */
    int result = cacheMatchAcl(&checklist->auth_user_request->user()->proxy_match_cache, checklist);
    checklist->auth_user_request = nullptr;
    return result;
}

