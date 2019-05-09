// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <script/sign.h>
#include <utilstrencodings.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
#include <univalue.h>

#include <boost/test/unit_test.hpp>
#include <test/test_bitcoin.h>
#include <wallet/test/wallet_test_fixture.h>

extern bool ParseHDKeypath(std::string keypath_str, std::vector<uint32_t>& keypath);

BOOST_FIXTURE_TEST_SUITE(psbt_wallet_tests, WalletTestingSetup)

BOOST_AUTO_TEST_CASE(psbt_updater_test)
{
    // Create prevtxs and add to wallet
    // hashMalFix : b2606c9d59b75ac8950a29d7071af8a258a5eb81415fe09a6bd74c56831f61e6
    // hash: 1dea7cd05979072a3578cab271c02244ea8a090bbb46aa680a65ecd027048d83
    CDataStream s_prev_tx1(ParseHex("0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000"), SER_NETWORK, PROTOCOL_VERSION);
    CTransactionRef prev_tx1;
    s_prev_tx1 >> prev_tx1;
    CWalletTx prev_wtx1(&m_wallet, prev_tx1);
    m_wallet.mapWallet.emplace(prev_wtx1.GetHash(), std::move(prev_wtx1));

    //hashMalFix : f5fcfbae61fd5753f4a223a6873ecb8dc0560e53d60f9245cf870e844d5a3766
    // hash: 75ddabb27b8845f5247975c8a5ba7c6f336c4570708ebe230caf6db5217ae858
    CDataStream s_prev_tx2(ParseHex("0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000"), SER_NETWORK, PROTOCOL_VERSION);
    CTransactionRef prev_tx2;
    s_prev_tx2 >> prev_tx2;
    CWalletTx prev_wtx2(&m_wallet, prev_tx2);
    m_wallet.mapWallet.emplace(prev_wtx2.GetHash(), std::move(prev_wtx2));

    // Add scripts
    CScript rs1;
    CDataStream s_rs1(ParseHex("475221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae"), SER_NETWORK, PROTOCOL_VERSION);
    s_rs1 >> rs1;
    m_wallet.AddCScript(rs1);

    CScript rs2;
    CDataStream s_rs2(ParseHex("2200208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b2028903"), SER_NETWORK, PROTOCOL_VERSION);
    s_rs2 >> rs2;
    m_wallet.AddCScript(rs2);

    CScript ws1;
    CDataStream s_ws1(ParseHex("47522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae"), SER_NETWORK, PROTOCOL_VERSION);
    s_ws1 >> ws1;
    m_wallet.AddCScript(ws1);

    // Add hd seed
    CKey key = DecodeSecret("5KSSJQ7UNfFGwVgpCZDSHm5rVNhMFcFtvWM3zQ8mW4qNDEN7LFd"); // Mainnet and uncompressed form of cUkG8i1RFfWGWy5ziR11zJ5V4U4W3viSFCfyJmZnvQaUsd1xuF3T
    CPubKey master_pub_key = m_wallet.DeriveNewSeed(key);
    m_wallet.SetHDSeed(master_pub_key);
    m_wallet.NewKeyPool();

    // Call FillPSBT
    PartiallySignedTransaction psbtx;
    CDataStream ssData(ParseHex("70736274ff01009a020000000266375a4d840e87cf45920fd6530e56c08dcb3e87a623a2f45357fd61aefbfcf50000000000ffffffffe6611f83564cd76b9ae05f4181eba558a2f81a07d7290a95c85ab7599d6c60b20100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f000000000000000000"), SER_NETWORK, PROTOCOL_VERSION);
    ssData >> psbtx;

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(*psbtx.tx);

    // Fill transaction with our data
    FillPSBT(&m_wallet, psbtx, &txConst, 1, false, true);

    // Get the final tx
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << psbtx;
    std::string final_hex = HexStr(ssTx.begin(), ssTx.end());
    BOOST_CHECK_EQUAL(final_hex, "70736274ff01009a020000000266375a4d840e87cf45920fd6530e56c08dcb3e87a623a2f45357fd61aefbfcf50000000000ffffffffe6611f83564cd76b9ae05f4181eba558a2f81a07d7290a95c85ab7599d6c60b20100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f00000000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f6187650000000104475221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae2206029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f10d90c6a4f000000800000008000000080220602dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d710d90c6a4f0000008000000080010000800001012000c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88701042200208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b2028903010547522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae2206023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7310d90c6a4f000000800000008003000080220603089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc10d90c6a4f00000080000000800200008000220203a9a4c37f5996d3aa25dbac6b570af0650394492942460b354753ed9eeca5877110d90c6a4f000000800000008004000080002202027f6399757d2eff55a136ad02c684b1838b6556e5f1b6b34282a94b6b5005109610d90c6a4f00000080000000800500008000");

    // Sign
    FillPSBT(&m_wallet, psbtx, &txConst, 1, true, true);
    CDataStream ssTx1(SER_NETWORK, PROTOCOL_VERSION);
    ssTx1 << psbtx;
    final_hex = HexStr(ssTx1.begin(), ssTx1.end());
    BOOST_CHECK_EQUAL(final_hex, "70736274ff01009a020000000266375a4d840e87cf45920fd6530e56c08dcb3e87a623a2f45357fd61aefbfcf50000000000ffffffffe6611f83564cd76b9ae05f4181eba558a2f81a07d7290a95c85ab7599d6c60b20100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f00000000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f6187650000000107d900473044022008bffc6bca1c8b25fb97f6e0950fac364884f5aedb5b96b27870920e26a51728022015fb456e05ad43e46c0f18a1b3f98ed5511b4254b20547fcc1c176a40605913801473044022027b098b02130c97dc5cdbaeaccf386e473aa240a728c9f4ceb7fc07dfe6b152502207e51d4eaf4b068e2f7b8afe1a61039b0a1d3beb5277acfb4380e1c8bcdab535501475221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae0001012000c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e8870107232200208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b20289030108da040047304402200ffcf29adaed934c7d5ebb934899acd575e221c4976411119b386c034c3fb6d702203a176821493e6142bb7f4242a2a75f09ca9c783f0d94ef112cc2e9ba5398b73d01473044022068d02d16f933450e19b27fa2e8b1e2eccf94d1c391dbbeeaee3b97493825c2e302200f829abe25868e65c63d3571256751cdb5fec789ecf843250a56c61f07f0b7340147522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae00220203a9a4c37f5996d3aa25dbac6b570af0650394492942460b354753ed9eeca5877110d90c6a4f000000800000008004000080002202027f6399757d2eff55a136ad02c684b1838b6556e5f1b6b34282a94b6b5005109610d90c6a4f00000080000000800500008000");
    //EncodeBase64((unsigned char*)ssTx1.data(), ssTx1.size());
}

BOOST_AUTO_TEST_CASE(parse_hd_keypath)
{
    std::vector<uint32_t> keypath;

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1", keypath));
    BOOST_CHECK(!ParseHDKeypath("///////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1'/1", keypath));
    BOOST_CHECK(!ParseHDKeypath("//////////////////////////'/", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/", keypath));
    BOOST_CHECK(!ParseHDKeypath("1///////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1'/", keypath));
    BOOST_CHECK(!ParseHDKeypath("1/'//////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("", keypath));
    BOOST_CHECK(!ParseHDKeypath(" ", keypath));

    BOOST_CHECK(ParseHDKeypath("0", keypath));
    BOOST_CHECK(!ParseHDKeypath("O", keypath));

    BOOST_CHECK(ParseHDKeypath("0000'/0000'/0000'", keypath));
    BOOST_CHECK(!ParseHDKeypath("0000,/0000,/0000,", keypath));

    BOOST_CHECK(ParseHDKeypath("01234", keypath));
    BOOST_CHECK(!ParseHDKeypath("0x1234", keypath));

    BOOST_CHECK(ParseHDKeypath("1", keypath));
    BOOST_CHECK(!ParseHDKeypath(" 1", keypath));

    BOOST_CHECK(ParseHDKeypath("42", keypath));
    BOOST_CHECK(!ParseHDKeypath("m42", keypath));

    BOOST_CHECK(ParseHDKeypath("4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1

    BOOST_CHECK(ParseHDKeypath("m", keypath));
    BOOST_CHECK(!ParseHDKeypath("n", keypath));

    BOOST_CHECK(ParseHDKeypath("m/", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0'", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0''", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0'/0'", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/'0/0'", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/0/0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0/00", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0/0/f00", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0/000000000000000000000000000000000000000000000000000000000000000000000000000000000000", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/1/1/111111111111111111111111111111111111111111111111111111111111111111111111111111111111", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/00/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0'/00/'0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/1/", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/1//", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("m/0/4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1

    BOOST_CHECK(ParseHDKeypath("m/4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("m/4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1
}

BOOST_AUTO_TEST_SUITE_END()
