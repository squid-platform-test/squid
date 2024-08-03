/*
 * Copyright (C) 1996-2024 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#ifndef SQUID_SRC_TESTS_TESTTOKENIZER_H
#define SQUID_SRC_TESTS_TESTTOKENIZER_H

#include "compat/cppunit.h"

class testTokenizer : public CPPUNIT_NS::TestFixture
{
    CPPUNIT_TEST_SUITE( testTokenizer );
    CPPUNIT_TEST ( testCharacterSet );
    CPPUNIT_TEST ( testTokenizerPrefix );
    CPPUNIT_TEST ( testTokenizerSuffix );
    CPPUNIT_TEST ( testTokenizerSkip );
    CPPUNIT_TEST ( testTokenizerToken );
    CPPUNIT_TEST ( testTokenizerInt64 );
    CPPUNIT_TEST_SUITE_END();

protected:
    void testTokenizerPrefix();
    void testTokenizerSuffix();
    void testTokenizerSkip();
    void testTokenizerToken();
    void testCharacterSet();
    void testTokenizerInt64();
};

#endif /* SQUID_SRC_TESTS_TESTTOKENIZER_H */

