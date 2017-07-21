/*
 * Copyright (C) 1996-2023 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_AUTH_FORWARD_H
#define SQUID_SRC_AUTH_FORWARD_H

#if USE_AUTH

#include <vector>

// TODO: make auth schedule AsyncCalls?
typedef void AUTHCB(void*);

/// HTTP Authentication
namespace Auth
{

class CredentialsCache;

class Scheme;
class SchemeConfig;
typedef std::vector<Auth::SchemeConfig *> ConfigVector;

class UserRequest;

} // namespace Auth

#endif /* USE_AUTH */
#endif /* SQUID_SRC_AUTH_FORWARD_H */

