/*
 * Copyright (C) 1996-2021 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_SECURITY_CERTIFICATE_H
#define SQUID_SRC_SECURITY_CERTIFICATE_H

#include "security/forward.h"

namespace Security
{

// The accessing/testing functions below require a non-constant Certificate when
// it is modified by an underlying library implementation (e.g., GnuTLS).

/// The SubjectName field of the given certificate (if found) or an empty SBuf.
SBuf SubjectName(Certificate &);

/// The Issuer field of the given certificate (if found) or an empty SBuf.
/// Some implementations modify the argument while searching (e.g., GnuTLS).
SBuf IssuerName(Certificate &);

/// \returns whether cert was (correctly) issued by the given issuer
/// Due to complexity of the underlying checks, it is impossible to clearly
/// distinguish pure negative answers (e.g., two independent certificates)
/// from errors (e.g., the issuer certificate lacks the right CA extension).
bool IsIssuedBy(Certificate &cert, Certificate &issuer);

/// convenience wrapper for checking self-signed certificates
inline bool IsSelfSigned(Certificate &c) { return IsIssuedBy(c, c); }

} // namespace Security

// Declared outside Security because all underlying Security::Certificate types
// are declared inside global namespace.
/// reports a one-line gist of the Certificate Subject Name (for debugging)
std::ostream &operator <<(std::ostream &, Security::Certificate &);

#endif /* SQUID_SRC_SECURITY_CERTIFICATE_H */

