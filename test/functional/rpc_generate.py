#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test generate RPC with provate keys parameter.

Check that generate RPC can generate blocks with proof/signatures using the private keys passed in using the newly added second argument - private keys

1) Test that the proof length is fixed - 64 bytes(128 hex str)
2) Verify proof
3) verify that proof generated using non-signer private keys are not accepted

invalidKey generated using "https://brainwalletx.github.io/#generator"

passphrase :"tapyrus"
privateKey : d61cb76ffd678c98320f6c9c5b341c41141add40d5804f6e7de4cabc9ab2f1c2
public key : 037150aad31bae4c20121de4e2355bdb4ae8c736cc24dd1d5489f6354d70329d3e
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

    def run_test(self):
        self.genesisblockhash = self.nodes[0].getbestblockhash()

        #test generate 0
        height = 0
        blocks = self.nodes[0].generate(0, self.signblockprivkey_wif)
        assert_equal(len(blocks), 0)
        hash = self.nodes[0].getbestblockhash()
        newblock = self.nodes[0].getblock(hash)

        assert_equal(hash, self.genesisblockhash)

        #test generate 1 - default private keys
        blocks = self.nodes[0].generate(1, self.signblockprivkey_wif)
        assert_equal(len(blocks), 1)
        newblock = self.nodes[0].getblock(blocks[0])
        height += 1
        assert_equal(newblock['hash'], blocks[0])
        assert_equal(newblock['previousblockhash'], self.genesisblockhash)
        assert_equal(newblock['height'], height)
        assert_equal(len(newblock['proof']), 128) #hex string length

        #test generate 10 - default private keys
        blocks = self.nodes[0].generate(10, self.signblockprivkey_wif)
        assert_equal(len(blocks), 10)
        for hash in blocks:
            height += 1
            newblock = self.nodes[0].getblock(hash)
            assert_equal(newblock['hash'], hash)
            assert_equal(newblock['height'], height)
            assert_equal(len(newblock['proof']), 128)

        #test generate 1 with 1 private key
        blocks = self.nodes[0].generate(1, self.signblockprivkey_wif)
        assert_equal(len(blocks), 1)
        newblock = self.nodes[0].getblock(blocks[0])
        height += 1
        assert_equal(newblock['hash'], blocks[0])
        assert_equal(newblock['height'], height)
        assert_equal(len(newblock['proof']), 128)
        
        #test generate 10 with 1 private key
        blocks = self.nodes[0].generate(10, self.signblockprivkey_wif)
        assert_equal(len(blocks), 10)
        for hash in blocks:
            height += 1
            newblock = self.nodes[0].getblock(hash)
            assert_equal(newblock['hash'], hash)
            assert_equal(newblock['height'], height)
            assert_equal(len(newblock['proof']), 128)
        
        #test error cases
        self.signblockprivkey_wif = "cUkubrEpPKnC7BniXkV8DCotfFFk5tpAh1GHRTT6q3PF5jqWYR3E" # invalid key
        assert_raises_rpc_error(-33, "Given private key doesn't correspond to the Aggregate Key.", self.nodes[0].generate, 1, self.signblockprivkey_wif)
        assert_raises_rpc_error(-33, "Given private key doesn't correspond to the Aggregate Key.", self.nodes[0].generate, 10, self.signblockprivkey_wif)

        self.signblockprivkey_wif = self.signblockprivkey_wif + "00"
        assert_raises_rpc_error(-12, "No private key given or invalid private key", self.nodes[0].generate, 10, self.signblockprivkey_wif)

        self.signblockprivkey_wif = self.signblockprivkey_wif[:-2] + "00"
        assert_raises_rpc_error(-12, "No private key given or invalid private key", self.nodes[0].generate, 10, self.signblockprivkey_wif)

        assert_raises_rpc_error(-12, "No private key given or invalid private key",self.nodes[0].generate)

if __name__ == '__main__':
    GenerateWithPrivateKeysTest().main()