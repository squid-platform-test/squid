/*
 * Copyright (C) 1996-2023 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#include "squid.h"
#include "format/Format.h"

#define STUB_API "stub_libformat.cc"
#include "tests/STUB.h"

void Format::Format::assemble(MemBuf &, const AccessLogEntryPointer &, int) const STUB
bool Format::Format::parse(char const*) STUB_RETVAL(false)
Format::Format::Format(char const*) STUB
Format::Format::~Format() STUB

#include "format/Quoting.h"
char *Format::QuoteUrlEncodeUsername(const char *) STUB_RETVAL(nullptr)
char *Format::QuoteMimeBlob(const char *) STUB_RETVAL(nullptr)
SBuf Format::DquoteString(const SBuf &) STUB_RETVAL(SBuf())
