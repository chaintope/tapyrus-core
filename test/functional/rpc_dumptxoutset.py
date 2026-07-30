#!/usr/bin/env python3
# Copyright (c) 2019-2022 The Bitcoin Core developers
# Copyright (c) 2024 Chaintope Inc
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the generation of UTXO snapshots using `dumptxoutset`.
"""

from test_framework.test_framework import BitcoinTestFramework
from  test_framework.blocktools import createTestGenesisBlock,  generate_blocks
from test_framework.mininode import P2PInterface
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    sha256sum_file,
    get_datadir_path,
    NetworkDirName,
    hex_str_to_bytes,
    TAPYRUS_NETWORK_PARAMS,
    TAPYRUS_MODES
)
from test_framework.messages import CSnapshotMetadata
from io import BytesIO
import os
import os.path
import tempfile

FILENAME = "dumptxoutset.dat"
TIME_GENESIS_BLOCK = 1296688602

class DumptxoutsetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.signblockprivkey = "c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d3"
        self.signblockprivkey_wif = "cUJN5RVzYWFoeY8rUztd47jzXCu1p57Ay8V7pqCzsBD3PEXN7Dd4"
        self.signblockpubkey = "03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d"
        self.genesisBlock = createTestGenesisBlock(self.signblockpubkey, self.signblockprivkey, nTime=TIME_GENESIS_BLOCK)

    def run_test(self):
        """Test a trivial usage of the dumptxoutset RPC command.
        Make the sequence of blocks deterministic by using fixed pubkey, privkey and mocktime """
        node = self.nodes[0]
        node.add_p2p_connection(P2PInterface(node.time_to_connect))
        mocktime = node.getblockheader(node.getblockhash(0))['time']
        node.setmocktime(mocktime)
        generate_blocks(100, node, hex_str_to_bytes(self.signblockpubkey),  self.signblockprivkey)
        self.sync_all()

        #create snapshot
        try:
            out = node.dumptxoutset(FILENAME)
        except Exception as e:
            self.log.error("Exception in dumptxoutset %s", e)
        expected_path = os.path.join(get_datadir_path(self.options.tmpdir, 0), NetworkDirName(), FILENAME)
        # recreating the same snapshot file causes error.
        assert_raises_rpc_error(
            -8, 'path already exists',  node.dumptxoutset, FILENAME)

        # verify stats
        assert_equal(out['coins_written'], 100)
        assert_equal(out['base_height'], 100)
        assert_equal(out['path'], str(expected_path))
        assert_equal(out['base_hash'], node.getblockhash(100))
        assert_equal(out['nchaintx'], 101)

        #these hashes should be deterministic
        assert_equal(out['txoutset_hash'], 'bcea571a9e349d39c6caf4fee9314d3619ce2aace5409a498d2643e20b25ab7a')
        assert_equal(out['base_hash'], '1ff6397566738ea5144e37a0b5e21ee06a9ccb52b5c3ff816518b8bcdd1270ab')

        # verify snapshot file
        assert os.path.exists(expected_path) and os.path.isfile(expected_path)
        assert_equal(
            sha256sum_file(str(expected_path)).hex(),
            '70b0cd5e35e7d0286bea9706d278a79fb6ea756cb24f426d3251b2571ba13662')

        # verify snapshot metadata
        snapshot = CSnapshotMetadata(out['base_hash'], out['coins_written'])
        with open(expected_path, 'rb') as f:
            data = f.read(68)
            snapshot.deserialize(BytesIO(data))

        assert_equal(snapshot.version, 1)
        assert_equal(snapshot.networkid, TAPYRUS_MODES.DEV.value)
        assert_equal(snapshot.network_mode, bytes(TAPYRUS_NETWORK_PARAMS[TAPYRUS_MODES.DEV][0], 'utf8'))
        assert_equal(snapshot.base_blockhash, out['base_hash'])
        assert_equal(snapshot.coins_count, out['coins_written'])

        # Specifying a path whose parent directory does not exist will fail.
        invalid_path = "nonexistent_dir/path"
        assert_raises_rpc_error(-8, "Path must be relative", node.dumptxoutset, "/tmp/foo/snap")
        assert_raises_rpc_error(-8, "Path must not contain '..'", node.dumptxoutset, "../snap")
        assert_raises_rpc_error(-8, "Couldn't open file temp file for writing",
            node.dumptxoutset, invalid_path)  # relative, write fails

        # A symlink inside the data directory that escapes to an outside directory
        # must be rejected even though the path itself looks relative and benign.
        outside_dir = tempfile.mkdtemp()
        network_datadir = os.path.join(get_datadir_path(self.options.tmpdir, 0), NetworkDirName())
        symlink_path = os.path.join(network_datadir, "escape_symlink")
        try:
            os.symlink(outside_dir, symlink_path)
            assert_raises_rpc_error(
                -8, "Path resolves outside the data directory",
                node.dumptxoutset, "escape_symlink/output.dat")
        finally:
            if os.path.islink(symlink_path):
                os.remove(symlink_path)
            os.rmdir(outside_dir)


if __name__ == '__main__':
    DumptxoutsetTest().main()
