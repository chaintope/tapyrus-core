// Copyright (c) 2018-2019 chaintope Inc.

#ifndef TAPYRUS_TEST_TEST_KEYS_HELPER_H
#define TAPYRUS_TEST_TEST_KEYS_HELPER_H

#include <pubkey.h>
#include <secp256k1.h>

// Mnemonic: candy maple cake sugar pudding cream honey rich smooth crumble sweet treat
// generate 16 private keys from drive path m/44'/60'/0'/0....
const std::vector<std::string> ValidPrivKeyStrings = {
        "c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d3",
        "ae6ae8e5ccbfb04590405997ee2d52d2b330726137b875053c36d94e974d162f",
        "0dbbe8e4ae425a6d2687f1a7e3ba17bc98c673636790f1b8ad91193c05875ef1",
        "c88b703fb08cbea894b6aeff5a544fb92e78a18e19814cd85da83b71f772aa6c",
        "388c684f0ba1ef5017716adb5d21a053ea8e90277d0868337519f97bede61418",
        "659cbb0e2411a44db63778987b1e22153c086a95eb6b18bdf89de078917abc63",
        "82d052c865f5763aad42add438569276c00d3d88a2d062d36b2bae914d58b8c8",
        "aa3680d5d48a8283413f7a108367c7299ca73f553735860a87b08f39395618b7",
        "0f62d96d6675f32685bbdb8ac13cda7c23436f63efbb9d07700d8669ff12b7c4",
        "8d5366123cb560bb606379f90a0bfd4769eecc0557f1b362dcae9012b548b1e5",
        "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b",
        "aa2c70c4b85a09be514292d04b27bbb0cc3f86d306d58fe87743d10a095ada07",
        "3087d8decc5f951f19a442397cf1eba1e2b064e68650c346502780b56454c6e2",
        "6125c8d4330941944cc6cc3e775e8620c479a5901ad627e6e734c6a6f7377428",
        "1c3e5453c0f9aa74a8eb0216310b2b013f017813a648fce364bf41dbc0b37647",
        "ea9fe9fd2f1761fc6f1f0f23eb4d4141d7b05f2b95a1b7a9912cd97bddd9036c"
};


// 16 public keys pair regarding of above.
const std::vector<std::string> ValidPubKeyStrings = {
        "03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d",
        "02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b900",
        "02785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e",
        "02396c2c8a22ec28dbe02613027edea9a3b0c314294985e09c2f389818b29fee06",
        "03e67ceb1f0af0ab4668227984782b48d286b88e54dc91487143199728d4597c02",
        "023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e",
        "0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a62",
        "0201c537fd7eb7928700927b48e51ceec621fc8ba1177ee2ad67336ed91e2f63a1",
        "033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e61",
        "02114e7960286099c603e51348df63fd0acb75f81b97a85eb4af87df9ee5ff18eb",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc",
        "02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf",
        "03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b",
        "02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a",
        "02e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be9",
        "02473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"
};

const std::string UncompressedPubKeyString = "046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd7";

std::string combinedPubkeyString(unsigned int keyCount);

std::vector<CPubKey> validPubKeys(unsigned int keyCount);

#endif //TAPYRUS_TEST_TEST_KEYS_HELPER_H
