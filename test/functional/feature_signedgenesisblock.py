#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that signed genesis block is read from genesis.dat file at node startup.

"genesis.dat" is in datadir.

This test checks that the node can start/exit as expected under the following scenarios:
1. Creating genesis.dat with valid and invalid Genesis block
2. Changing genesis.dat file after creation
3. Corrupting genesis.dat file after the blockchain has progressed and recovering it and the blockchain

"""

import os
import shutil
import time

from test_framework.test_framework import BitcoinTestFramework, initialize_datadir
from test_framework.test_node import ErrorMatch
from test_framework.mininode import P2PInterface
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
from test_framework.util import (
    assert_equal
)

def createIncorectGenesisBlock(genesis_coinbase, signblockprivkey, signblockpubkey):
    genesis = CBlock()
    genesis.nTime = int(time.time() + 600)
    genesis.hashPrevBlock = 0
    genesis.vtx.append(genesis_coinbase)
    genesis.hashMerkleRoot = genesis.calc_merkle_root()
    genesis.hashImMerkleRoot = genesis.calc_immutable_merkle_root()
    genesis.xfieldType = 1
    genesis.xfield = hex_str_to_bytes(signblockpubkey)
    genesis.solve(signblockprivkey)
    return genesis

def writeIncorrectGenesisBlockToFile(datadir, genesis):
    with open(os.path.join(datadir, "genesis.dat"), 'w', encoding='utf8') as f:
        f.write(bytes_to_hex_str(genesis.serialize()))

def createGenesisCoinbase(signblockpubkey):
    genesis_coinbase = CTransaction()
    coinbaseinput = CTxIn(outpoint=COutPoint(0, 0), nSequence=0xffffffff)
    coinbaseinput.scriptSig=CScript([hex_str_to_bytes(signblockpubkey)])
    genesis_coinbase.vin.append(coinbaseinput)
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = 50 * COIN
    coinbaseoutput.scriptPubKey = CScript([OP_DUP, OP_HASH160, hex_str_to_bytes(signblockpubkey),OP_EQUALVERIFY, OP_CHECKSIG])
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

        initialize_datadir(self.options.tmpdir, 0)

        self.log.info("Test with no genesis file")
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: unable to read genesis file', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Phase 1: Tests using genesis block")
        self.log.info("Test correct genesis file")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        self.start_node(0)
        self.stop_node(0)

        self.log.info("Restart with correct genesis file")
        self.start_node(0)
        self.stop_node(0)

        self.log.info("Test incorrect genesis block - No Coinbase")
        genesis_coinbase = createGenesisCoinbase(self.signblockpubkey)
        genesis_coinbase.vin[0].prevout.hash = 111111
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkey, self.signblockpubkey)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - Incorrect height")
        genesis_coinbase_height = createGenesisCoinbase(self.signblockpubkey)
        genesis_coinbase_height.vin[0].prevout.n = 10
        genesis = createIncorectGenesisBlock(genesis_coinbase_height, self.signblockprivkey, self.signblockpubkey)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid height in genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - Multiple transactions")
        genesis_coinbase = createGenesisCoinbase(self.signblockpubkey)
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkey, self.signblockpubkey)
        genesis.vtx.append(CTransaction())
        genesis.hashMerkleRoot = genesis.calc_merkle_root()
        genesis.hashImMerkleRoot = genesis.calc_immutable_merkle_root()
        genesis.solve(self.signblockprivkey)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - No proof")
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkey, self.signblockpubkey)
        genesis.proof.clear()

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - Insufficient Proof")
        genesis = createIncorectGenesisBlock(genesis_coinbase, self.signblockprivkey, self.signblockpubkey)
        genesis.proof = genesis.proof[:-1]

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - Incorrect xfieldType")
        genesis_coinbase = createGenesisCoinbase(self.signblockpubkey)
        genesis = CBlock()
        genesis.nTime = int(time.time() + 600)
        genesis.hashPrevBlock = 0
        genesis.vtx.append(genesis_coinbase)
        genesis.hashMerkleRoot = genesis.calc_merkle_root()
        genesis.hashImMerkleRoot = genesis.calc_immutable_merkle_root()
        genesis.xfieldType = 0
        genesis.xfield = hex_str_to_bytes(self.signblockpubkey)
        genesis.solve(self.signblockprivkey)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid xfieldType in genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - Incorrect xfield")
        genesis_coinbase = createGenesisCoinbase(self.signblockpubkey)
        genesis = CBlock()
        genesis.nTime = int(time.time() + 600)
        genesis.hashPrevBlock = 0
        genesis.vtx.append(genesis_coinbase)
        genesis.hashMerkleRoot = genesis.calc_merkle_root()
        genesis.hashImMerkleRoot = genesis.calc_immutable_merkle_root()
        genesis.xfieldType = 1
        genesis.xfield = hex_str_to_bytes(self.signblockpubkey[:32])
        genesis.solve(self.signblockprivkey)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'Aggregate Public Key for Signed Block is invalid', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - No hashMerkleRoot")
        genesis_coinbase = createGenesisCoinbase(self.signblockpubkey)
        genesis = CBlock()
        genesis.nTime = int(time.time() + 600)
        genesis.hashPrevBlock = 0
        genesis.vtx.append(genesis_coinbase)
        genesis.xfieldType = 1
        genesis.xfield = hex_str_to_bytes(self.signblockpubkey)
        # not populating hashMerkleRoot and hashImMerkleRoot
        genesis.solve(self.signblockprivkey)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid MerkleRoot in genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Test incorrect genesis block - No hashImMerkleRoot")
        genesis_coinbase = createGenesisCoinbase(self.signblockpubkey)
        genesis = CBlock()
        genesis.nTime = int(time.time() + 600)
        genesis.hashPrevBlock = 0
        genesis.vtx.append(genesis_coinbase)
        genesis.hashMerkleRoot = genesis.calc_merkle_root()
        genesis.xfieldType = 1
        genesis.xfield = hex_str_to_bytes(self.signblockpubkey)
        # not populating hashImMerkleRoot
        genesis.solve(self.signblockprivkey)

        writeIncorrectGenesisBlockToFile(self.nodes[0].datadir, genesis)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid MerkleRoot in genesis block', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Phase 2: Tests using genesis.dat file")
        self.log.info("Test new genesis file")
        self.genesisBlock = None
        self.writeGenesisBlockToFile(self.nodes[0].datadir, nTime=int(time.time()))
        #different genesis file
        self.nodes[0].assert_start_raises_init_error([], 'Error: Incorrect or no genesis block found.', match=ErrorMatch.PARTIAL_REGEX)

        datadir = self.nodes[0].datadir
        genesisFile = os.path.join(datadir, "genesis.dat")

        self.log.info("Test incorrect genesis file - append 2 bytes")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'a', encoding='utf8') as f:
            f.write("abcd")
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test incorrect genesis file - append many bytes")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'a', encoding='utf8') as f:
            s = "".join([str(i) for i in range(0,16) for j in range(0, 100)])
            f.write(s)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test incorrect genesis file - replace 2 bytes")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'r+', encoding='utf8') as f:
            content = f.readline()
            clen = len(content)
            content = content[:500] + "0000" + content[504:]
            assert(len(content) == clen)
            f.write(content)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test incorrect genesis file - insert 2 bytes")
        content = ""
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'r+', encoding='utf8') as f:
            content = f.readline()
            clen = len(content)
            content = content[:550] + "1111" + content[550:]
            assert(len(content) == clen + 4)
            f.write(content)
        self.nodes[0].assert_start_raises_init_error([], 'ReadGenesisBlock: invalid genesis file', match=ErrorMatch.PARTIAL_REGEX)
        os.remove(genesisFile)

        self.log.info("Test incorrect genesis file - remove 2 bytes")
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

        self.log.info("Test incorrect genesis file - truncate file")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        with open(genesisFile, 'r+', encoding='utf8') as f:
            f.truncate(500)
        self.nodes[0].assert_start_raises_init_error([], 'CDataStream::read().*end of data', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Phase 3: Edit genesis file after sarting the blockchain")
        self.stop_node(0)
        shutil.rmtree(self.nodes[0].datadir)
        initialize_datadir(self.options.tmpdir, 0)

        self.log.info("Starting node")
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        self.start_node(0)
        self.nodes[0].add_p2p_connection(P2PInterface())

        self.log.info("Generating 10 blocks")
        blocks = self.nodes[0].generate(10, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:1]])
        assert_equal(self.nodes[0].getbestblockhash(), blocks[-1])
        self.stop_node(0)
        shutil.copytree(self.nodes[0].datadir, os.path.join(self.options.tmpdir, "backup")) 

        self.log.info("Creating corrupt genesis file")
        with open(genesisFile, 'r+', encoding='utf8') as f:
            content = f.readline()
            clen = len(content)
            content = content[:500] + "0000" + content[504:]
            assert(len(content) == clen)
            f.write(content)
        self.nodes[0].assert_start_raises_init_error([])

        self.log.info("Starting node again")
        self.genesisBlock = None
        self.writeGenesisBlockToFile(self.nodes[0].datadir)
        self.nodes[0].assert_start_raises_init_error([], 'Error: Incorrect or no genesis block found.', match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Recovering original blockchain")
        shutil.rmtree(self.nodes[0].datadir)
        shutil.copytree(os.path.join(self.options.tmpdir, "backup"), self.nodes[0].datadir)
        self.start_node(0)
        self.nodes[0].add_p2p_connection(P2PInterface())
        self.sync_all([self.nodes[0:1]])

        assert_equal(self.nodes[0].getbestblockhash(), blocks[-1])
        self.log.info("Blockchain intact!")

if __name__ == '__main__':
    SignedGenesisBlockTest().main()
