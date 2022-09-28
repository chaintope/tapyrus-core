#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the getnewblock RPC.

Test parameter xfield_type:xfield

xfield_type 1 - Aggregate Public key
xfield_type 2 - Max block size

"""

from email import header
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal,assert_raises_rpc_error
from test_framework.messages import FromHex, CBlock

class GetNewBlockTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.supports_cli = True

    def run_test (self):

        self.log.info("Test starting...")
        self.nodes[0].generate(1, self.signblockprivkey_wif)

        self.log.info("getnewblock regression tests")
        address = self.nodes[0].getnewaddress()
        self.nodes[0].getnewblock(address)
        self.nodes[0].getnewblock(address, 1)
        self.nodes[0].getnewblock(address, 0)
        self.nodes[0].getnewblock("mt8EZJFAhhhxv57NFYfXPecDoAbWWqnRqX")

        assert_raises_rpc_error(-1, "JSON value is not a string as expected", self.nodes[0].getnewblock)
        assert_raises_rpc_error(-1, "JSON value is not a string as expected", self.nodes[0].getnewblock, -10)
        assert_raises_rpc_error(-32602, "required_wait must be non-negative", self.nodes[0].getnewblock, address, -10)
        assert_raises_rpc_error(-5, "Error: Invalid address", self.nodes[0].getnewblock, "hjgyj")
        assert_raises_rpc_error(-5, "Error: Invalid address", self.nodes[0].getnewblock, address+ "00")
        assert_raises_rpc_error(-1, "JSON integer out of range", self.nodes[0].getnewblock, address, 565786879879785)

        self.log.info("getnewblock aggpubkey tests")
        blockhex = self.nodes[0].getnewblock(address, 0,"1:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        block = FromHex(CBlock(), blockhex)
        assert_equal(block.xfieldType, 1)
        assert_equal(block.xfield, bytes.fromhex("03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"))

        blockhex = self.nodes[0].getnewblock(address, 10,"1:02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf")
        block = FromHex(CBlock(), blockhex)
        assert_equal(block.xfieldType, 1)
        assert_equal(block.xfield, bytes.fromhex("02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf"))

        self.log.info("getnewblock aggpubkey negative tests")
        assert_raises_rpc_error(-32602, "xfield parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "Unknown xfield type", self.nodes[0].getnewblock, address, 0,"2:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "Unknown xfield type", self.nodes[0].getnewblock, address, 0,"0:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "Unknown xfield type", self.nodes[0].getnewblock, address, 0,"2:831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "Unknown xfield type", self.nodes[0].getnewblock, address, 0,"2:03ac")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. Aggregate public key could not be parsed", self.nodes[0].getnewblock, address, 0,"1:fghjgf131423fafc")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. Aggregate public key could not be parsed", self.nodes[0].getnewblock, address, 0,"1:831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafz")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. Aggregate public key was uncompressed or invalid", self.nodes[0].getnewblock, address, 0,"1:831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. Aggregate public key was uncompressed or invalid", self.nodes[0].getnewblock, address, 0,"1:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fa")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. Aggregate public key was uncompressed or invalid", self.nodes[0].getnewblock, address, 0,"1:0383fc")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. Aggregate public key was uncompressed or invalid", self.nodes[0].getnewblock, address, 0,"1:831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. Aggregate public key was uncompressed or invalid", self.nodes[0].getnewblock, address, 0,"1:0496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858ee")

        self.log.info("getnewblock maxblocksize tests")
        address = self.nodes[0].getnewaddress()
        blockhex = self.nodes[0].getnewblock(address, 0,"2:400000")
        block = FromHex(CBlock(), blockhex)
        assert_equal(block.xfieldType, 2)
        assert_equal(block.xfield, 400000)

        blockhex = self.nodes[0].getnewblock(address, 10,"2:1000")
        block = FromHex(CBlock(), blockhex)
        assert_equal(block.xfieldType, 2)
        assert_equal(block.xfield, 1000)

        self.log.info("getnewblock maxblocksize negative tests")
        assert_raises_rpc_error(-32602, "xfield max block size was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:")
        assert_raises_rpc_error(-32602, "xfield max block size was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:0")
        assert_raises_rpc_error(-32602, "xfield max block size was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:-3898798798797897")
        assert_raises_rpc_error(-32602, "xfield max block size was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:-1")
        assert_raises_rpc_error(-32602, "xfield max block size was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:567587687987998")
        assert_raises_rpc_error(-32602, "xfield max block size was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:-0")
        assert_raises_rpc_error(-32602, "xfield_type parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"3:0")

if __name__ == '__main__':
    GetNewBlockTest().main ()