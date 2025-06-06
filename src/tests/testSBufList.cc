/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#include "squid.h"
#include "compat/cppunit.h"
#include "sbuf/Algorithms.h"
#include "sbuf/List.h"
#include "unitTestMain.h"

class TestSBufList : public CPPUNIT_NS::TestFixture
{
    CPPUNIT_TEST_SUITE(TestSBufList);
    CPPUNIT_TEST(testSBufListMembership);
    CPPUNIT_TEST(testSBufListJoin);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testSBufListMembership();
    void testSBufListJoin();
};
CPPUNIT_TEST_SUITE_REGISTRATION( TestSBufList );

SBuf literal("The quick brown fox jumped over the lazy dog");
static int sbuf_tokens_number=9;
static SBuf tokens[]= {
    SBuf("The",3), SBuf("quick",5), SBuf("brown",5), SBuf("fox",3),
    SBuf("jumped",6), SBuf("over",4), SBuf("the",3), SBuf("lazy",4),
    SBuf("dog",3)
};

void
TestSBufList::testSBufListMembership()
{
    SBufList foo;
    for (int j=0; j<sbuf_tokens_number; ++j)
        foo.push_back(tokens[j]);
    CPPUNIT_ASSERT_EQUAL(true,IsMember(foo,SBuf("fox")));
    CPPUNIT_ASSERT_EQUAL(true,IsMember(foo,SBuf("Fox"),caseInsensitive));
    CPPUNIT_ASSERT_EQUAL(false,IsMember(foo,SBuf("garble")));
}

void
TestSBufList::testSBufListJoin()
{
    SBufList foo;
    CPPUNIT_ASSERT_EQUAL(SBuf(""),JoinContainerToSBuf(foo.begin(), foo.end(),SBuf()));
    for (int j = 0; j < sbuf_tokens_number; ++j)
        foo.push_back(tokens[j]);
    SBuf joined=JoinContainerToSBuf(foo.begin(), foo.end(),SBuf(" "));
    CPPUNIT_ASSERT_EQUAL(literal,joined);
    SBuf s1("1"), s2("2"), s3("3"), full("(1,2,3)");
    SBufList sl {s1,s2,s3};
    CPPUNIT_ASSERT_EQUAL(full, JoinContainerToSBuf(sl.begin(),
                         sl.end(), SBuf(","), SBuf("("), SBuf(")")));

    CPPUNIT_ASSERT_EQUAL(SBuf(""),JoinContainerToSBuf(foo.begin(), foo.begin(),SBuf()));

}

int
main(int argc, char *argv[])
{
    return TestProgram().run(argc, argv);
}

