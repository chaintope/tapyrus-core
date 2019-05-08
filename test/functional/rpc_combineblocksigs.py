#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test signed-blockchain-related RPC calls:

    - combineblocksigs

"""

from decimal import Decimal
from test_framework.key import CECKey
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, hex_str_to_bytes, bytes_to_hex_str, assert_raises_rpc_error
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import ToHex
from time import time

class SignedBlockchainTest(BitcoinTestFramework):

    def set_test_params(self):
        """Tests must this method to change default values for number of nodes, topology, etc"""
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.cKey = []
        self.pubkeys = []
        self.secret = [
        "0dbbe8e4ae425a6d2687f1a7e3ba17bc98c673636790f1b8ad91193c05875ef1",
        "c88b703fb08cbea894b6aeff5a544fb92e78a18e19814cd85da83b71f772aa6c",
        "388c684f0ba1ef5017716adb5d21a053ea8e90277d0868337519f97bede61418",
        "659cbb0e2411a44db63778987b1e22153c086a95eb6b18bdf89de078917abc63",
        "82d052c865f5763aad42add438569276c00d3d88a2d062d36b2bae914d58b8c8",
        "aa3680d5d48a8283413f7a108367c7299ca73f553735860a87b08f39395618b7",
        "0f62d96d6675f32685bbdb8ac13cda7c23436f63efbb9d07700d8669ff12b7c4"]

        signblockpubkeys = "-signblockpubkeys="

        for i in range(0, len(self.secret)):
            self.cKey.append(CECKey())
            self.cKey[i].set_secretbytes(hex_str_to_bytes(self.secret[i]))
            self.cKey[i].set_compressed(True)
            self.pubkeys.append(self.cKey[i].get_pubkey())

            signblockpubkeys = signblockpubkeys + bytes_to_hex_str(self.pubkeys[i])

        self.extra_args = [[signblockpubkeys], [signblockpubkeys], [signblockpubkeys]]

    def run_test(self):
        self.log.info("Running combineblocksigs tests")
        assert_equal(self.nodes[0].getblockcount(), 0)
        assert_equal(self.nodes[1].getblockcount(), 0)
        previousblock_hash = int(self.nodes[0].getbestblockhash(), 16)

        # create a block
        height =  1
        coinbase = create_coinbase(height, self.pubkeys[0])
        block = create_block(previousblock_hash, coinbase, int(time()))
        block.solve()
        block.calc_sha256()
        block_hex = ToHex(block)
        block_hash = block.getsighash()

        self.log.info("Test block : %s" % bytes_to_hex_str(block_hash))

        self.log.info("Testing RPC combineblocksigs parameter validation")
        #invalid block hex
        assert_raises_rpc_error(-22, "Block decode failed", self.nodes[0].combineblocksigs,"0000", [])
        #no signature
        assert_raises_rpc_error(-32602, "Signature list was empty", self.nodes[0].combineblocksigs,block_hex, [])
        #too many signature
        assert_raises_rpc_error(-32602, "Too many signatures", self.nodes[0].combineblocksigs,block_hex, ["00","00","00","00","00","00","00","00","00","00"])
        
        sig0 = self.cKey[0].sign(block_hash)
        sig1 = self.cKey[1].sign(block_hash)
        sig2 = self.cKey[2].sign(block_hash)

        self.log.info("Testing RPC combineblocksigs with 3 valid signatures")
        signedBlock = self.nodes[0].combineblocksigs(block_hex, [bytes_to_hex_str(sig0),bytes_to_hex_str(sig1),bytes_to_hex_str(sig2)])

        if(len(signedBlock["warning"])):
            self.log.warning("%s : signatures:%s [%d, %d, %d]" % (signedBlock["warning"], [sig0,sig1,sig2],
            self.cKey[0].verify(block.sighash,sig0),
            self.cKey[1].verify(block.sighash,sig1),
            self.cKey[2].verify(block.sighash,sig2)))

        # combineblocksigs only returns true when signatures are appended and enough
        # are included to pass validation
        assert_equal(signedBlock["complete"], True)
        assert_equal(signedBlock["warning"], "")
        assert_equal(len(signedBlock["hex"]), len(block_hex) + len(bytes_to_hex_str(sig0))+ len(bytes_to_hex_str(sig1))+ len(bytes_to_hex_str(sig2)) + 6)
        #6 is 2 bytes for 3 length of signatures

        self.log.info("Testing RPC combineblocksigs with 1 invalid signature")
        sig0 = bytearray(sig0)
        sig0[2] = 30

        signedBlock = self.nodes[0].combineblocksigs(block_hex, [bytes_to_hex_str(sig0),bytes_to_hex_str(sig1),bytes_to_hex_str(sig2)])

        assert_equal(signedBlock["complete"], True) #True as threshold is 1
        assert_equal(signedBlock["warning"], "invalid encoding in signature: Non-canonical DER signature %s One or more signatures were not added to block" % ( bytes_to_hex_str(sig0) ))
        assert_equal(len(signedBlock["hex"]), len(block_hex) + len(bytes_to_hex_str(sig1))+ len(bytes_to_hex_str(sig2)) + 4)

        self.log.info("Testing RPC combineblocksigs with 2 invalid signatures")
        sig1 = bytearray(sig1)
        sig1[2] = 30

        signedBlock = self.nodes[0].combineblocksigs(block_hex, [bytes_to_hex_str(sig0),bytes_to_hex_str(sig1),bytes_to_hex_str(sig2)])

        assert_equal(signedBlock["complete"], True) #True as threshold is 1
        assert_equal(len(signedBlock["hex"]), len(block_hex)+ len(bytes_to_hex_str(sig2)) + 2)

        self.log.info("Testing RPC combineblocksigs with 3 invalid signatures")
        sig2 = bytearray(sig2)
        sig2[2] = 30

        signedBlock = self.nodes[0].combineblocksigs(block_hex, [bytes_to_hex_str(sig0),bytes_to_hex_str(sig1),bytes_to_hex_str(sig2)])

        assert_equal(signedBlock["complete"], False)
        assert_equal(len(signedBlock["hex"]), len(block_hex))


if __name__ == '__main__':
    SignedBlockchainTest().main()
