#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test signed-blockchain-related RPC calls:

    - combineblocksigs
    - testproposedblock

"""

from decimal import Decimal
from test_framework.key import CECKey
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, hex_str_to_bytes, bytes_to_hex_str, assert_raises_rpc_error
from test_framework.blocktools import create_block, create_coinbase, create_tx_with_script
from test_framework.messages import ToHex, CTransaction, CTxIn, COutPoint, CTxOut
from test_framework.mininode import P2PDataStore
from test_framework.script import CScript, SignatureHash, SIGHASH_ALL
from time import time

class SignedBlockchainTest(BitcoinTestFramework):

    def set_test_params(self):
        """Tests must this method to change default values for number of nodes, topology, etc"""
        self.setup_clean_chain = True
        self.num_nodes = 1
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

        self.extra_args = [[signblockpubkeys]]

    def run_test(self):
        self.test_node = self.nodes[0].add_p2p_connection(P2PDataStore())

        self.log.info("Running signed block tests")
        assert_equal(self.nodes[0].getblockcount(), 0)
        genesisblock_hash = int(self.nodes[0].getbestblockhash(), 16)

        # create 1 spendable coinbase
        height = 1
        block_time = int(time())
        block1 = create_block(genesisblock_hash, create_coinbase(height), block_time)
        block1.solve()
        self.nodes[0].p2p.send_blocks_and_test([block1], self.nodes[0], success=True)
        self.nodes[0].generate(100)
        previousblock_hash = int(self.nodes[0].getbestblockhash(), 16)

        # create a test block
        height =  102
        block_time = int(time())
        block = create_block(previousblock_hash, create_coinbase(height), block_time + 100)
        block.solve()
        block_hex = ToHex(block)
        block_hash = block.getsighash()
        previousblock_hash = int(self.nodes[0].getbestblockhash(), 16)

        self.log.info("Test block : %s" % bytes_to_hex_str(block_hash))

        self.log.info("Testing RPC testproposedblock with valid block")

        assert_equal(self.nodes[0].testproposedblock(block_hex), True)
        self.nodes[0].p2p.send_blocks_and_test([block], self.nodes[0], success=True)

        self.log.info("Testing RPC combineblocksigs with 3 valid signatures")
        sig0 = self.cKey[0].sign(block_hash)
        sig1 = self.cKey[1].sign(block_hash)
        sig2 = self.cKey[2].sign(block_hash)

        signedBlock = self.nodes[0].combineblocksigs(block_hex, [bytes_to_hex_str(sig0),bytes_to_hex_str(sig1),bytes_to_hex_str(sig2)])

        if(len(signedBlock["warning"])):
            self.log.warning("%s : signatures:%s [%d, %d, %d]" % (signedBlock["warning"], [sig0,sig1,sig2],
            self.cKey[0].verify(block_hash,sig0),
            self.cKey[1].verify(block_hash,sig1),
            self.cKey[2].verify(block_hash,sig2)))

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

        self.log.info("Testing RPC combineblocksigs  with invalid blocks")
        #invalid block hex
        assert_raises_rpc_error(-22, "Block decode failed", self.nodes[0].combineblocksigs,"0000", [])
        #no signature
        assert_raises_rpc_error(-32602, "Signature list was empty", self.nodes[0].combineblocksigs,block_hex, [])
        #too many signature
        assert_raises_rpc_error(-32602, "Too many signatures", self.nodes[0].combineblocksigs,block_hex, ["00","00","00","00","00","00","00","00","00","00"])

        self.log.info("Testing RPC testproposedblock with old block")
        #invalid block hex
        assert_raises_rpc_error(-22, "Block decode failed", self.nodes[0].testproposedblock,"0000")
        # create invalid block
        height =  103
        invalid_block = create_block(previousblock_hash, create_coinbase(height), block_time + 110)
        invalid_block.solve()
        assert_raises_rpc_error(-25, "proposal was not based on our best chain", self.nodes[0].testproposedblock, ToHex(invalid_block))

        self.log.info("Testing RPC testproposedblock with non standard block")
        # create block with non-standard transaction
        previousblock_hash = int(self.nodes[0].getbestblockhash(), 16)
        height =  103
        nonstd_block = create_block(previousblock_hash, create_coinbase(height), block_time + 120)
        # 3 of 2 multisig is non standard
        nonstd_script = CScript([b'53' + self.pubkeys[0] + self.pubkeys[1] + self.pubkeys[2]+ b'52ea'])
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(block1.vtx[0].malfixsha256, 0), b""))
        tx.vout.append(CTxOut(50, b'0x51'))
        (sig_hash, err) = SignatureHash(nonstd_script, tx, 0, SIGHASH_ALL)
        signature = self.cKey[0].sign(sig_hash) + b'\x01'  # 0x1 is SIGHASH_ALL
        tx.vin[0].scriptSig = CScript([signature, self.pubkeys[0]])
        tx.rehash()
        #add non-standard tx to block
        nonstd_block.vtx.append(tx)
        nonstd_block.solve()
        nonstd_block.hashMerkleRoot = nonstd_block.calc_merkle_root()
        nonstd_block.hashImMerkleRoot = nonstd_block.calc_immutable_merkle_root()
        assert(nonstd_block.is_valid())

        #block is accepted when acceptnonstd flag is set
        assert_equal(self.nodes[0].testproposedblock(ToHex(nonstd_block), True), True)

        #block is rejected when acceptnonstd flag is not set
        assert_raises_rpc_error(-25, "Block proposal included a non-standard transaction", self.nodes[0].testproposedblock, ToHex(nonstd_block), False)


if __name__ == '__main__':
    SignedBlockchainTest().main()
