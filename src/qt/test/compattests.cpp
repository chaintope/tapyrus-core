// Copyright (c) 2016-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tapyrus-config.h>

#include <qt/test/compattests.h>

#include <compat/byteswap.h>

void CompatTests::bswapTests()
{
	// Sibling in bitcoin/src/test/bswap_tests.cpp
	uint16_t u1 = 0x1234;
	uint32_t u2 = 0x56789abc;
	uint64_t u3 = 0xdef0123456789abc;
	uint16_t e1 = 0x3412;
	uint32_t e2 = 0xbc9a7856;
	uint64_t e3 = 0xbc9a78563412f0de;
	QVERIFY(internal_bswap_16(u1) == e1);
	QVERIFY(internal_bswap_32(u2) == e2);
	QVERIFY(internal_bswap_64(u3) == e3);
}
