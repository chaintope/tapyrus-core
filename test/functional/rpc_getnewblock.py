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

    def run_test (self):

        self.log.info("Test starting...")
        self.nodes[0].generate(1, self.signblockprivkey_wif)

        self.log.info("getnewblock regression tests")
        address = self.nodes[0].getnewaddress()
        blockhex = self.nodes[0].getnewblock(address)
        blockhex = self.nodes[0].getnewblock(address, 1)
        assert_raises_rpc_error(-1, "JSON value is not a string as expected", self.nodes[0].getnewblock)

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
        assert_raises_rpc_error(-32602, "xfield_type parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield_type parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield_type parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"0:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield_type parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield_type parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"2:03ac")

        assert_raises_rpc_error(-32602, "xfield parameter was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"1:831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"1:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fa")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"1:0383fc")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"1:831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc")
        assert_raises_rpc_error(-32602, "xfield parameter was invalid. It is expected to be <xfield_type:new_xfield_value>", self.nodes[0].getnewblock, address, 0,"1:0496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858ee")



if __name__ == '__main__':
    GetNewBlockTest().main ()