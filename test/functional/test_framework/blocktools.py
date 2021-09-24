#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Utilities for manipulating blocks and transactions."""

from .messages import (
    CBlock,
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    FromHex,
    ToHex,
    bytes_to_hex_str,
    hash256,
    hex_str_to_bytes,
    ser_string,
    ser_uint256,
    sha256,
    uint256_from_str,
)
from .script import (
    CScript,
    OP_0,
    OP_1,
    OP_CHECKMULTISIG,
    OP_CHECKSIG,
    OP_RETURN,
    OP_TRUE,
    hash160,
    OP_DUP,
    OP_HASH160,
    OP_EQUALVERIFY,
    OP_CHECKSIG,
    OP_COLOR,
    OP_EQUAL
)
from .util import assert_equal
from io import BytesIO
import time, random
from enum import Enum

# From BIP141
WITNESS_COMMITMENT_HEADER = b"\xaa\x21\xa9\xed"

def create_block(hashprev, coinbase, ntime, signblockpubkey=""):
    """Create a block (with regtest difficulty)."""
    block = CBlock()
    if ntime is None:
        block.nTime = int(time.time() + 600)
    else:
        block.nTime = ntime
    block.hashPrevBlock = hashprev
    block.vtx.append(coinbase)
    block.hashMerkleRoot = block.calc_merkle_root()
    block.hashImMerkleRoot = block.calc_immutable_merkle_root()
    if(signblockpubkey != ""):
        block.xfieldType = 1
        block.xfield = hex_str_to_bytes(signblockpubkey)
    else:
        block.xfieldType = 0
        block.xfield = b''
    block.calc_sha256()
    return block

# create test genesis block
def createTestGenesisBlock(signblockpubkey, signblockprivkey, nTime=None):
    genesis_coinbase = CTransaction()
    coinbaseinput = CTxIn(outpoint=COutPoint(0, 0), nSequence=0xffffffff)
    coinbaseinput.scriptSig=CScript([hex_str_to_bytes(signblockpubkey)])
    genesis_coinbase.vin.append(coinbaseinput)
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = 50 * COIN
    coinbaseoutput.scriptPubKey = CScript([OP_DUP, OP_HASH160, hex_str_to_bytes(signblockpubkey),OP_EQUALVERIFY, OP_CHECKSIG])
    genesis_coinbase.vout.append(coinbaseoutput)
    genesis_coinbase.calc_sha256()

    genesis = CBlock()
    if nTime is None or nTime <= 0:
        genesis.nTime = int(time.time() - 600)
    else:
        genesis.nTime = nTime
    genesis.hashPrevBlock = 0
    genesis.vtx.append(genesis_coinbase)
    genesis.hashMerkleRoot = genesis.calc_merkle_root()
    genesis.hashImMerkleRoot = genesis.calc_immutable_merkle_root()
    genesis.xfieldType = 1
    genesis.xfield = hex_str_to_bytes(signblockpubkey)
    genesis.solve(signblockprivkey)
    return genesis

def get_witness_script(witness_root, witness_nonce):
    witness_commitment = uint256_from_str(hash256(ser_uint256(witness_root) + ser_uint256(witness_nonce)))
    output_data = WITNESS_COMMITMENT_HEADER + ser_uint256(witness_commitment)
    return CScript([OP_RETURN, output_data])

def add_witness_commitment(block, nonce=0):
    """Add a witness commitment to the block's coinbase transaction.

    According to BIP141, blocks with witness rules active must commit to the
    hash of all in-block transactions including witness."""
    # First calculate the merkle root of the block's
    # transactions, with witnesses.
    witness_nonce = nonce
    witness_root = block.calc_witness_merkle_root()
    # witness_nonce should go to coinbase witness.
    block.vtx[0].wit.vtxinwit = [CTxInWitness()]
    block.vtx[0].wit.vtxinwit[0].scriptWitness.stack = [ser_uint256(witness_nonce)]

    # witness commitment is the last OP_RETURN output in coinbase
    block.vtx[0].vout.append(CTxOut(0, get_witness_script(witness_root, witness_nonce)))
    block.vtx[0].rehash()
    block.hashMerkleRoot = block.calc_merkle_root()
    block.hashImMerkleRoot = block.calc_immutable_merkle_root()
    block.rehash()

def serialize_script_num(value):
    r = bytearray(0)
    if value == 0:
        return r
    neg = value < 0
    absvalue = -value if neg else value
    while (absvalue):
        r.append(int(absvalue & 0xff))
        absvalue >>= 8
    if r[-1] & 0x80:
        r.append(0x80 if neg else 0)
    elif neg:
        r[-1] |= 0x80
    return r

def create_coinbase(height, pubkey=None):
    """Create a coinbase transaction, assuming no miner fees.

    If pubkey is passed in, the coinbase output will be a P2PK output;
    otherwise an anyone-can-spend output."""
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(outpoint=COutPoint(0, height), nSequence=0xffffffff))
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = 50 * COIN
    halvings = int(height / 150)  # regtest
    coinbaseoutput.nValue >>= halvings
    if (pubkey is not None):
        coinbaseoutput.scriptPubKey = CScript([pubkey, OP_CHECKSIG])
    else:
        coinbaseoutput.scriptPubKey = CScript([OP_TRUE])
    coinbase.vout = [coinbaseoutput]
    coinbase.calc_sha256()
    return coinbase

def create_tx_with_script(prevtx, n, script_sig=b"", *, amount, script_pub_key=CScript()):
    """Return one-input, one-output transaction object
       spending the prevtx's n-th output with the given amount.

       Can optionally pass scriptPubKey and scriptSig, default is anyone-can-spend ouput.
    """
    tx = CTransaction()
    assert(n < len(prevtx.vout))
    tx.vin.append(CTxIn(COutPoint(prevtx.malfixsha256, n), script_sig, 0xffffffff))
    tx.vout.append(CTxOut(amount, script_pub_key))
    tx.calc_sha256()
    return tx

def create_transaction(node, txid, to_address, *, amount):
    """ Return signed transaction spending the first output of the
        input txid. Note that the node must be able to sign for the
        output that is being spent, and the node must not be running
        multiple wallets.
    """
    raw_tx = create_raw_transaction(node, txid, to_address, amount=amount)
    tx = CTransaction()
    tx.deserialize(BytesIO(hex_str_to_bytes(raw_tx)))
    return tx

def create_raw_transaction(node, txid, to_address, *, amount):
    """ Return raw signed transaction spending the first output of the
        input txid. Note that the node must be able to sign for the
        output that is being spent, and the node must not be running
        multiple wallets.
    """
    scheme = random.choice(["ECDSA", "SCHNORR"])
    rawtx = node.createrawtransaction(inputs=[{"txid": txid, "vout": 0}], outputs={to_address: amount})
    signresult = node.signrawtransactionwithwallet(rawtx, [], "ALL", scheme)
    assert_equal(signresult["complete"], True)
    return signresult['hex']

# Colored coin definitions
##########################
def TOKEN_TYPES(Enum):
    NONE = 0
    REISSUABLE = 1
    NONREISSUABLE = 2
    NFT = 3

def findTPC(list):
    for item in list:
        if item['token'] == 'TPC':
            return item

def create_colored_transaction(token_type, amount, node, issue=True, colorId=None, to_node=None):
    tpc_utxo = findTPC(node.listunspent())
    if(issue):
        if(token_type == 1):
            return node.issuetoken(token_type, amount, tpc_utxo['scriptPubKey'])
        else:
            return node.issuetoken(token_type, amount, tpc_utxo['txid'], tpc_utxo['vout'])
    else:
        if(colorId == None or to_node == None):
            raise ("colorId and to_node parameters are required when transfering token")
        to_address = to_node.getnewaddress(color=colorId)
        return node.sendtoaddress(to_address, amount)

def get_legacy_sigopcount_block(block, accurate=True):
    count = 0
    for tx in block.vtx:
        count += get_legacy_sigopcount_tx(tx, accurate)
    return count

def get_legacy_sigopcount_tx(tx, accurate=True):
    count = 0
    for i in tx.vout:
        count += i.scriptPubKey.GetSigOpCount(accurate)
    for j in tx.vin:
        # scriptSig might be of type bytes, so convert to CScript for the moment
        count += CScript(j.scriptSig).GetSigOpCount(accurate)
    return count

def witness_script(use_p2wsh, pubkey):
    """Create a scriptPubKey for a pay-to-wtiness TxOut.

    This is either a P2WPKH output for the given pubkey, or a P2WSH output of a
    1-of-1 multisig for the given pubkey. Returns the hex encoding of the
    scriptPubKey."""
    if not use_p2wsh:
        # P2WPKH instead
        pubkeyhash = hash160(hex_str_to_bytes(pubkey))
        pkscript = CScript([OP_0, pubkeyhash])
    else:
        # 1-of-1 multisig
        witness_program = CScript([OP_1, hex_str_to_bytes(pubkey), OP_1, OP_CHECKMULTISIG])
        scripthash = sha256(witness_program)
        pkscript = CScript([OP_0, scripthash])
    return bytes_to_hex_str(pkscript)

def create_witness_tx(node, use_p2wsh, utxo, pubkey, amount):
     """Return a transaction (in hex) that spends the given utxo to a segwit output.

     Optionally wrap the segwit output using P2SH."""
     if use_p2wsh:
         program = CScript([OP_1, hex_str_to_bytes(pubkey), OP_1, OP_CHECKMULTISIG])
         addr = script_to_p2sh_p2wsh(program)
     else:
         addr = key_to_p2sh_p2wpkh(pubkey)
     return node.createrawtransaction([utxo], {addr: amount})

def send_to_witness(use_p2wsh, node, utxo, pubkey, encode_p2sh, amount, sign=True, insert_redeem_script=""):
    """Create a transaction spending a given utxo to a segwit output.

    The output corresponds to the given pubkey: use_p2wsh determines whether to
    use P2WPKH or P2WSH; encode_p2sh determines whether to wrap in P2SH.
    sign=True will have the given node sign the transaction.
    insert_redeem_script will be added to the scriptSig, if given."""

    scheme = random.choice(["ECDSA", "SCHNORR"])
    tx_to_witness = create_witness_tx(node, use_p2wsh, utxo, pubkey, amount)
    if (sign):
        signed = node.signrawtransactionwithwallet(tx_to_witness, [], "ALL", scheme)
        if(encode_p2sh):
            assert("errors" not in signed or len(["errors"]) == 0)
        return node.sendrawtransaction(signed["hex"])
    else:
        if (insert_redeem_script):
            tx = FromHex(CTransaction(), tx_to_witness)
            tx.vin[0].scriptSig += CScript([hex_str_to_bytes(insert_redeem_script)])
            tx_to_witness = ToHex(tx)

    return node.sendrawtransaction(tx_to_witness)