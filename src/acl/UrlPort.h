/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_ACL_URLPORT_H
#define SQUID_SRC_ACL_URLPORT_H

#include "acl/Data.h"
#include "acl/ParameterizedNode.h"

namespace Acl
{

/// a "port" ACL
class UrlPortCheck: public ParameterizedNode< ACLData<int> >
{
public:
    /* Acl::Node API */
    int match(ACLChecklist *) override;
    bool requiresRequest() const override {return true;}
};

} // namespace Acl

#endif /* SQUID_SRC_ACL_URLPORT_H */

