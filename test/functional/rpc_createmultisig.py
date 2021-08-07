#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test transaction signing using the signrawtransaction* RPCs."""

from test_framework.test_framework import BitcoinTestFramework
import decimal

class RpcCreateMultiSigTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def get_keys(self):
        node0,node1,node2 = self.nodes
        self.add = [node1.getnewaddress() for _ in range(self.nkeys)]
        self.pub = [node1.getaddressinfo(a)["pubkey"] for a in self.add]
        self.priv = [node1.dumpprivkey(a) for a in self.add]
        self.final = node2.getnewaddress()

    def run_test(self):
        node0,node1,node2 = self.nodes

        # 50 TPC starting balance
        node0.generate(1, self.signblockprivkey_wif)

        self.sync_all()

        self.moved = 0
        for self.nkeys in [3,5]:
            for self.nsigs in [2,3]:
                self.get_keys()
                self.do_multisig()

        self.checkbalances()

    def checkbalances(self):
        node0,node1,node2 = self.nodes

        bal0 = node0.getbalance()
        bal1 = node1.getbalance()
        bal2 = node2.getbalance()

        height = node0.getblockchaininfo()["blocks"]
        assert height == 9 # initial 1 + 8 blocks mined
        assert bal0+bal1+bal2 == 9*50

        # bal0 is initial 50 + total_block_rewards - self.moved - fee paid (total_block_rewards - 400)
        assert bal0 == 450 - self.moved
        assert bal1 == 0
        assert bal2 == self.moved

    def do_multisig(self):
        node0,node1,node2 = self.nodes

        msig = node2.createmultisig(self.nsigs, self.pub)
        madd = msig["address"]
        mredeem = msig["redeemScript"]

        # compare against addmultisigaddress
        msigw = node1.addmultisigaddress(self.nsigs, self.pub, None)
        maddw = msigw["address"]
        mredeemw = msigw["redeemScript"]
        # addmultisigiaddress and createmultisig work the same
        assert maddw == madd
        assert mredeemw == mredeem

        txid = node0.sendtoaddress(madd, 40)

        tx = node0.getrawtransaction(txid, True)
        vout = [v["n"] for v in tx["vout"] if madd in v["scriptPubKey"].get("addresses",[])]
        assert len(vout) == 1
        vout = vout[0]
        scriptPubKey = tx["vout"][vout]["scriptPubKey"]["hex"]
        value = tx["vout"][vout]["value"]
        prevtxs = [{"txid": txid, "vout": vout, "scriptPubKey": scriptPubKey, "redeemScript": mredeem, "amount": value}]

        node0.generate(1, self.signblockprivkey_wif)

        outval = value - decimal.Decimal("0.00001000")
        rawtx = node2.createrawtransaction([{"txid": txid, "vout": vout}], [{self.final: outval}])

        rawtx2 = node2.signrawtransactionwithkey(rawtx, self.priv[0:self.nsigs-1], prevtxs, "ALL", self.options.scheme)
        rawtx3 = node2.signrawtransactionwithkey(rawtx2["hex"], [self.priv[-1]], prevtxs, "ALL", self.options.scheme)

        self.moved += outval
        tx = node0.sendrawtransaction(rawtx3["hex"], True)
        blk = node0.generate(1, self.signblockprivkey_wif)[0]
        assert tx in node0.getblock(blk)["tx"]

        txinfo = node0.getrawtransaction(tx, True, blk)
        self.log.info("n/m=%d/%d size=%d vsize=%d weight=%d" % (self.nsigs, self.nkeys, txinfo["size"], txinfo["vsize"], txinfo["weight"]))

if __name__ == '__main__':
    RpcCreateMultiSigTest().main()
