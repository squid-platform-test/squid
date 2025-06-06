/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_ACL_ANNOTATETRANSACTION_H
#define SQUID_SRC_ACL_ANNOTATETRANSACTION_H

#include "acl/Note.h"

namespace Acl
{

/// an "annotate_transaction" ACL
class AnnotateTransactionCheck: public Acl::AnnotationCheck
{
public:
    /* Acl::Node API */
    int match(ACLChecklist *) override;
};

} // namespace Acl

#endif /* SQUID_SRC_ACL_ANNOTATETRANSACTION_H */

