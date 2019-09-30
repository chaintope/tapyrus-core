#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test generate RPC with provate keys parameter.

Check that generate RPC can generate blocks with proof/signatures using the private keys passed in using the newly added second argument - private keys

1) Test that the proof length is equal to the length of private keys
2) Verify the signatures in proof
3) verify that valid signatures generated using non-signer private keys are not accepted

invalidKeys generated using "https://brainwalletx.github.io/#generator"

passphrase :"tapyrus"
privateKey : d61cb76ffd678c98320f6c9c5b341c41141add40d5804f6e7de4cabc9ab2f1c2
public key : 037150aad31bae4c20121de4e2355bdb4ae8c736cc24dd1d5489f6354d70329d3e
passphrase :"tapyrus1"
privateKey : e81469726d9662adea8a47f4f91cace6309e97635e4a5c7ee34792fba293c6e7
public key : 03a5290286cec13e7dff0e9015c295f542f93ec0ddd7b1aaf4011718b2781d090e
passphrase :"tapyrus2"
privateKey : 2403c6a86bda076901ed94fb705f40b2c5229efe3d1e079a23cdae8c0eae6121
public key : 0366baa3dda0f960b12519172ebcd5c68d0b958f46bef25843d5b8bd5581f7360d
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.key import CECKey, CPubKey
from test_framework.test_node import ErrorMatch
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    bytes_to_hex_str,
    hex_str_to_bytes
)

class GenerateWithPrivateKeysTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.supports_cli = True

        self.pubkeys = [
        CPubKey(hex_str_to_bytes ("037150aad31bae4c20121de4e2355bdb4ae8c736cc24dd1d5489f6354d70329d3e")), 
        CPubKey(hex_str_to_bytes("03a5290286cec13e7dff0e9015c295f542f93ec0ddd7b1aaf4011718b2781d090e")), 
        CPubKey(hex_str_to_bytes("0366baa3dda0f960b12519172ebcd5c68d0b958f46bef25843d5b8bd5581f7360d"))]

        self.invalidKeys = [
            "d61cb76ffd678c98320f6c9c5b341c41141add40d5804f6e7de4cabc9ab2f1c2",
            "e81469726d9662adea8a47f4f91cace6309e97635e4a5c7ee34792fba293c6e7",
            "2403c6a86bda076901ed94fb705f40b2c5229efe3d1e079a23cdae8c0eae6121"]

        self.privateKeys = [
        "c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d3",
        "ae6ae8e5ccbfb04590405997ee2d52d2b330726137b875053c36d94e974d162f",
        "0dbbe8e4ae425a6d2687f1a7e3ba17bc98c673636790f1b8ad91193c05875ef1",
        "c88b703fb08cbea894b6aeff5a544fb92e78a18e19814cd85da83b71f772aa6c",
        "388c684f0ba1ef5017716adb5d21a053ea8e90277d0868337519f97bede61418",
        "659cbb0e2411a44db63778987b1e22153c086a95eb6b18bdf89de078917abc63"]  

    def run_test(self):
        self.genesisblockhash = self.nodes[0].getbestblockhash()

        #test generate 0
        height = 0
        blocks = self.nodes[0].generate(0, self.signblockprivkeys)
        assert_equal(len(blocks), 0)
        hash = self.nodes[0].getbestblockhash()
        newblock = self.nodes[0].getblock(hash)

        assert_equal(hash, self.genesisblockhash)

        #test generate 1 - default private keys
        blocks = self.nodes[0].generate(1, self.signblockprivkeys)
        assert_equal(len(blocks), 1)
        newblock = self.nodes[0].getblock(blocks[0])
        height += 1
        assert_equal(newblock['hash'], blocks[0])
        assert_equal(newblock['previousblockhash'], self.genesisblockhash)
        assert_equal(newblock['height'], height)
        assert_equal(len(newblock['proof']), self.signblockthreshold)

        #test generate 10 - default private keys
        blocks = self.nodes[0].generate(10, self.signblockprivkeys)
        assert_equal(len(blocks), 10)
        for hash in blocks:
            height += 1
            newblock = self.nodes[0].getblock(hash)
            assert_equal(newblock['hash'], hash)
            assert_equal(newblock['height'], height)
            assert_equal(len(newblock['proof']), self.signblockthreshold)

        #test generate 1 with 1 private key
        self.signblockprivkeys = [self.privateKeys[0]]
        blocks = self.nodes[0].generate(1, self.signblockprivkeys)
        assert_equal(len(blocks), 1)
        newblock = self.nodes[0].getblock(blocks[0])
        height += 1
        assert_equal(newblock['hash'], blocks[0])
        assert_equal(newblock['height'], height)
        assert_equal(len(newblock['proof']), 1)
        
        #test generate 10 with 1 private key
        self.signblockprivkeys = [self.privateKeys[0]]
        blocks = self.nodes[0].generate(10, self.signblockprivkeys)
        assert_equal(len(blocks), 10)
        for hash in blocks:
            height += 1
            newblock = self.nodes[0].getblock(hash)
            assert_equal(newblock['hash'], hash)
            assert_equal(newblock['height'], height)
            assert_equal(len(newblock['proof']), 1)
        
        #test generate 1 with multiple private keys
        self.signblockprivkeys = self.privateKeys
        blocks = self.nodes[0].generate(1, self.signblockprivkeys)
        newblock = self.nodes[0].getblock(blocks[0])
        height += 1
        assert_equal(newblock['hash'], blocks[0])
        assert_equal(newblock['height'], height)
        assert_equal(len(newblock['proof']), len(self.privateKeys))
        
        #test generate 10 with multiple private keys
        self.signblockprivkeys = self.privateKeys
        blocks = self.nodes[0].generate(10, self.signblockprivkeys)
        for hash in blocks:
            height += 1
            newblock = self.nodes[0].getblock(hash)
            assert_equal(newblock['hash'], hash)
            assert_equal(newblock['height'], height)
            assert_equal(len(newblock['proof']), len(self.privateKeys))
        
        #test error cases
        self.signblockprivkeys = [self.invalidKeys[0]]
        assert_raises_rpc_error(-32603, "AbsorbBlockProof, block proof not accepted", self.nodes[0].generate, 1, self.signblockprivkeys)
        assert_raises_rpc_error(-32603, "AbsorbBlockProof, block proof not accepted", self.nodes[0].generate, 10, self.signblockprivkeys)

        self.signblockprivkeys = self.invalidKeys
        assert_raises_rpc_error(-32603, "AbsorbBlockProof, block proof not accepted", self.nodes[0].generate, 1, self.signblockprivkeys)
        assert_raises_rpc_error(-32603, "AbsorbBlockProof, block proof not accepted", self.nodes[0].generate, 10, self.signblockprivkeys)

        self.signblockprivkeys = [self.privateKeys[0] + "00"]
        assert_raises_rpc_error(-12, "Error: key 'c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d300' is invalid length of 66.", self.nodes[0].generate, 10, self.signblockprivkeys)

        self.signblockprivkeys = [self.privateKeys[0][:-2] + "00"]
        assert_raises_rpc_error(-32603, "AbsorbBlockProof, block proof not accepted", self.nodes[0].generate, 10, self.signblockprivkeys)

        assert_raises_rpc_error(-12, "No private key given or all keys were invalid.",self.nodes[0].generate)

if __name__ == '__main__':
    GenerateWithPrivateKeysTest().main()