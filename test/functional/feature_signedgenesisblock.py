#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the signed genesis block read from genesis.dat file.

1. test framework uses regtest mode. this is not suitable for our test.
"""

import os
import shutil
import time

from test_framework.test_framework import BitcoinTestFramework, initialize_datadir
from test_framework.test_node import ErrorMatch
from test_framework.messages import (
    CBlock,
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    bytes_to_hex_str,
    hex_str_to_bytes
)
from test_framework.script import (
    CScript,
    OP_DUP,
    OP_HASH160,
    OP_EQUALVERIFY,
    OP_CHECKSIG
)

def createIncorectGenesisBlock(genesis_coinbase, signblockprivkeys):
    genesis = CBlock()
    genesis.nTime = int(time.time() + 600)
    genesis.hashPrevBlock = 0
    genesis.vtx.append(genesis_coinbase)
    genesis.hashMerkleRoot = genesis.calc_merkle_root()
    genesis.hashImMerkleRoot = genesis.calc_immutable_merkle_root()
    genesis.solve(signblockprivkeys)
    return genesis

def writeIncorrectGenesisBlockToFile(datadir, genesis):
    with open(os.path.join(datadir, "regtest", "genesis.dat"), 'w', encoding='utf8') as f:
        f.write(bytes_to_hex_str(genesis.serialize()))

def createGenesisCoinbase(signblockthreshold, signblockpubkeys):
    genesis_coinbase = CTransaction()
    coinbaseinput = CTxIn(outpoint=COutPoint(0, 0), nSequence=0xffffffff)
    coinbaseinput.scriptSig=CScript([bytes(signblockthreshold), hex_str_to_bytes(signblockpubkeys)])
    genesis_coinbase.vin.append(coinbaseinput)
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = 50 * COIN
    coinbaseoutput.scriptPubKey = CScript([OP_DUP, OP_HASH160, hex_str_to_bytes(signblockpubkeys[:64]),OP_EQUALVERIFY, OP_CHECKSIG])
    genesis_coinbase.vout.append(coinbaseoutput)
    genesis_coinbase.calc_sha256()
    return genesis_coinbase

class SignedGenesisBlockTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1


    def run_test(self):
        self.stop_node(0)
        shutil.rmtree(self.nodes[0].datadir)

        initialize_datadir(self.options.tmpdir, 0, self.signblockpubkeys, self.signblockthreshold)

        self.log.info("Test with no genesis file")
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: unable to read genesis file', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Phase 1: Tests using genesis block")
        self.log.info("Test correct genesis file")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        self.start_node(0)
        self.stop_node(0)

        self.log.info("Test incorrect genesis file - No Coinbase")
        genesis_coinbase = createGenesisCoinbase(self.signblockthreshold, self.signblockpubkeys)
        genesis_coinbase.vin[0].prevout.hash = 111111
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkeys)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis file - Incorrect height")
        genesis_coinbase_height = createGenesisCoinbase(self.signblockthreshold, self.signblockpubkeys)
        genesis_coinbase_height.vin[0].prevout.n = 10
        genesis = createIncorectGenesisBlock(genesis_coinbase_height, self.signblockprivkeys)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid height in genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis file - Multiple transactions")
        genesis_coinbase = createGenesisCoinbase(self.signblockthreshold, self.signblockpubkeys)
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkeys)
        genesis.vtx.append(CTransaction())
        genesis.hashMerkleRoot = genesis.calc_merkle_root()
        genesis.hashImMerkleRoot = genesis.calc_immutable_merkle_root()
        genesis.solve(self.signblockprivkeys)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis file - No proof")
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkeys)
        genesis.proof.clear()

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis file - Insufficient Proof")
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkeys)
        genesis.proof = genesis.proof[:self.signblockthreshold - 1]

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis file - No hashMerkleRoot")
        genesis_coinbase = createGenesisCoinbase(self.signblockthreshold, self.signblockpubkeys)
        genesis = CBlock()
        genesis.nTime = int(time.time() + 600)
        genesis.hashPrevBlock = 0
        genesis.vtx.append(genesis_coinbase)
        # not populating hashMerkleRoot and hashImMerkleRoot
        genesis.solve(self.signblockprivkeys)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid MerkleRoot in genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis file - No hashImMerkleRoot")
        genesis_coinbase = createGenesisCoinbase(self.signblockthreshold, self.signblockpubkeys)
        genesis = CBlock()
        genesis.nTime = int(time.time() + 600)
        genesis.hashPrevBlock = 0
        genesis.vtx.append(genesis_coinbase)
        genesis.hashMerkleRoot = genesis.calc_merkle_root()
        # not populating hashImMerkleRoot
        genesis.solve(self.signblockprivkeys)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid MerkleRoot in genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Phase 2: Tests using genesis.dat file")
        """self.log.info("Test correct genesis file")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        self.start_node(0)
        self.stop_node(0)"""

        datadir = os.path.join(self.nodes[0].datadir, "regtest")
        genesisFile = os.path.join(datadir, "genesis.dat")

        self.log.info("Test Edit genesis.dat - append 2 bytes")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'a', encoding='utf8') as f:
            f.write("abcd")
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test Edit genesis.dat - append many bytes")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'a', encoding='utf8') as f:
            s = "".join([str(i) for i in range(0,16) for j in range(0, 100)])
            f.write(s)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test Edit genesis.dat - replace 2 bytes")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'r+', encoding='utf8') as f:
            content = f.readline()
            clen = len(content)
            content = content[:1000] + "0000" + content[1004:]
            assert(len(content) == clen)
            f.write(content)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test Edit genesis.dat - insert 2 bytes")
        content = ""
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'r+', encoding='utf8') as f:
            content = f.readline()
            clen = len(content)
            content = content[:2000] + "1111" + content[2000:]
            assert(len(content) == clen + 4)
            f.write(content)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test Edit genesis.dat - remove 2 bytes")
        content = ""
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'r+', encoding='utf8') as f:
            content = f.readline()
            clen = len(content)
            content = content[:100] + content[104:]
            assert(len(content) == clen - 4)
            f.write(content)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test Edit genesis.dat - truncate file")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'r+', encoding='utf8') as f:
            f.truncate(500)
        self.nodes[0].assert_start_raises_init_error([], 'end of data: unspecified iostream_category error', match=ErrorMatch.PARTIAL_REGEX)

if __name__ == '__main__':
    SignedGenesisBlockTest().main()
