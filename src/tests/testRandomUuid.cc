/*
 * Copyright (C) 1996-2022 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#include "squid.h"
#include "base/RandomUuid.h"
#include "compat/cppunit.h"
#include "sbuf/SBuf.h"
#include "sbuf/Stream.h"
#include "unitTestMain.h"

#include <map>

class TestRandomUuid: public CPPUNIT_NS::TestFixture
{
    CPPUNIT_TEST_SUITE( TestRandomUuid );
    CPPUNIT_TEST( testUniqueness );
    CPPUNIT_TEST( testSerialization );
    CPPUNIT_TEST( testStringRepresentation );
    CPPUNIT_TEST( testInvalidIds );
    CPPUNIT_TEST_SUITE_END();

protected:
    void testUniqueness();
    void testSerialization();
    void testStringRepresentation();
    void testInvalidIds();
};

CPPUNIT_TEST_SUITE_REGISTRATION( TestRandomUuid );

typedef std::map<SBuf, RandomUuid::Serialized> RandomIds;

// Generated by https://www.uuidgenerator.net/version4
// binary representation of the generated UUID in network byte order
static const RandomIds ExternalIds {
    { SBuf("bd1b1c07-f7fa-428a-b019-7e390133b0e5"), { 0xbd, 0x1b, 0x1c, 0x07, 0xf7, 0xfa, 0x42, 0x8a, 0xb0, 0x19, 0x7e, 0x39, 0x01, 0x33, 0xb0, 0xe5 } },
    { SBuf("f63ccd5a-9d25-41a5-a36c-a7b0c6b5c678"), { 0xf6, 0x3c, 0xcd, 0x5a, 0x9d, 0x25, 0x41, 0xa5, 0xa3, 0x6c, 0xa7, 0xb0, 0xc6, 0xb5, 0xc6, 0x78 } },
    { SBuf("9c8363ac-9c62-44e9-941f-86b7edc25dc7"), { 0x9c, 0x83, 0x63, 0xac, 0x9c, 0x62, 0x44, 0xe9, 0x94, 0x1f, 0x86, 0xb7, 0xed, 0xc2, 0x5d, 0xc7 } }
};

static const RandomIds InvalidIds {
    { SBuf("00000000-0000-0000-0000-000000000000"), { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { SBuf("ffffffff-ffff-ffff-ffff-ffffffffffff"), { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } }
};

void
TestRandomUuid::testUniqueness()
{
    for (int i = 0; i < 1000; ++i) {
        CPPUNIT_ASSERT_MESSAGE("UUID are unique", RandomUuid() != RandomUuid());
    }
}

void
TestRandomUuid::testSerialization()
{
    RandomUuid uuid;
    CPPUNIT_ASSERT_MESSAGE("Original and deserialized UUIDs are equal", uuid == RandomUuid(uuid.serialize()));
}

void
TestRandomUuid::testStringRepresentation()
{
    for (const auto &id: ExternalIds) {
        CPPUNIT_ASSERT_MESSAGE("Original and generated UUID string representations are the same", id.first == ToSBuf(id.second));
    }
}

void
TestRandomUuid::testInvalidIds()
{
    for (const auto &id: InvalidIds) {
        try {
            RandomUuid uuid(id.second);
            std::cerr << std::endl
                      << "FAIL: " << id.first
                      << Debug::Extra << "error: should be rejected" << std::endl;
        } catch (const TextException &e) {
            continue; // success, caught a malformed UUID
        }
        CPPUNIT_FAIL("failed to reject an invalid UUID");
    }
}

