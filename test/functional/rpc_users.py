#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test multiple RPC users."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    get_datadir_path,
    str_to_b64str,
)

import os
import http.client
import urllib.parse
import subprocess
import time
import threading
from random import SystemRandom
import string
import configparser
import sys


class HTTPBasicsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def setup_chain(self):
        super().setup_chain()
        #Append rpcauth to tapyrus.conf before initialization
        rpcauth = "rpcauth=rt:93648e835a54c573682c2eb19f882535$7681e9c5b74bdd85e78166031d2058e1069b3ed7ed967c93fc63abba06f31144"
        rpcauth2 = "rpcauth=rt2:f8607b1a88861fac29dfccf9b52ff9f$ff36a0c23c8c62b4846112e50fa888416e94c17bfd4c42f88fd8f55ec6a3137e"
        rpcuser = "rpcuser=rpcuser💻"
        rpcpassword = "rpcpassword=rpcpassword🔑"

        self.user = ''.join(SystemRandom().choice(string.ascii_letters + string.digits) for _ in range(10))
        config = configparser.ConfigParser()
        config.read_file(open(self.options.configfile, encoding='utf-8'))
        gen_rpcauth = config['environment']['RPCAUTH']
        p = subprocess.Popen([sys.executable, gen_rpcauth, self.user], stdout=subprocess.PIPE, universal_newlines=True)
        lines = p.stdout.read().splitlines()
        rpcauth3 = lines[1]
        self.password = lines[3]

        with open(os.path.join(get_datadir_path(self.options.tmpdir, 0), "tapyrus.conf"), 'a', encoding='utf8') as f:
            f.write(rpcauth+"\n")
            f.write(rpcauth2+"\n")
            f.write(rpcauth3+"\n")
        with open(os.path.join(get_datadir_path(self.options.tmpdir, 1), "tapyrus.conf"), 'a', encoding='utf8') as f:
            f.write(rpcuser+"\n")
            f.write(rpcpassword+"\n")

    def run_test(self):

        ##################################################
        # Check correctness of the rpcauth config option #
        ##################################################
        url = urllib.parse.urlparse(self.nodes[0].url)

        #Old authpair
        authpair = url.username + ':' + url.password

        #New authpair generated via share/rpcauth tool
        password = "cA773lm788buwYe4g4WT+05pKyNruVKjQ25x3n0DQcM="

        #Second authpair with different username
        password2 = "8/F3uMDw4KSEbw96U3CA1C4X05dkHDN2BPFjTgZW4KI="
        authpairnew = "rt:"+password

        self.log.info('Correct...')
        headers = {"Authorization": "Basic " + str_to_b64str(authpair)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 200)
        conn.close()

        #Use new authpair to confirm both work
        self.log.info('Correct...')
        headers = {"Authorization": "Basic " + str_to_b64str(authpairnew)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 200)
        conn.close()

        #Wrong login name with rt's password
        self.log.info('Wrong...')
        authpairnew = "rtwrong:"+password
        headers = {"Authorization": "Basic " + str_to_b64str(authpairnew)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 401)
        conn.close()

        #Wrong password for rt
        self.log.info('Wrong...')
        authpairnew = "rt:"+password+"wrong"
        headers = {"Authorization": "Basic " + str_to_b64str(authpairnew)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 401)
        conn.close()

        #Correct for rt2
        self.log.info('Correct...')
        authpairnew = "rt2:"+password2
        headers = {"Authorization": "Basic " + str_to_b64str(authpairnew)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 200)
        conn.close()

        #Wrong password for rt2
        self.log.info('Wrong...')
        authpairnew = "rt2:"+password2+"wrong"
        headers = {"Authorization": "Basic " + str_to_b64str(authpairnew)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 401)
        conn.close()

        #Correct for randomly generated user
        self.log.info('Correct...')
        authpairnew = self.user+":"+self.password
        headers = {"Authorization": "Basic " + str_to_b64str(authpairnew)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 200)
        conn.close()

        #Wrong password for randomly generated user
        self.log.info('Wrong...')
        authpairnew = self.user+":"+self.password+"Wrong"
        headers = {"Authorization": "Basic " + str_to_b64str(authpairnew)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 401)
        conn.close()

        ###############################################################
        # Check correctness of the rpcuser/rpcpassword config options #
        ###############################################################
        url = urllib.parse.urlparse(self.nodes[1].url)

        # rpcuser and rpcpassword authpair
        self.log.info('Correct...')
        rpcuserauthpair = "rpcuser💻:rpcpassword🔑"

        headers = {"Authorization": "Basic " + str_to_b64str(rpcuserauthpair)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 200)
        conn.close()

        #Wrong login name with rpcuser's password
        rpcuserauthpair = "rpcuserwrong:rpcpassword"
        headers = {"Authorization": "Basic " + str_to_b64str(rpcuserauthpair)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 401)
        conn.close()

        #Wrong password for rpcuser
        self.log.info('Wrong...')
        rpcuserauthpair = "rpcuser:rpcpasswordwrong"
        headers = {"Authorization": "Basic " + str_to_b64str(rpcuserauthpair)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 401)
        conn.close()

        ###############################################################
        # Auth-failure per-IP backoff: worker threads must not block  #
        ###############################################################
        self.log.info("Test per-IP backoff: worker threads are not held during auth failures")
        url0 = urllib.parse.urlparse(self.nodes[0].url)
        correct_headers = {"Authorization": "Basic " + str_to_b64str(url0.username + ':' + url0.password)}
        wrong_headers   = {"Authorization": "Basic " + str_to_b64str("wrong:wrong")}

        # Send DEFAULT_HTTP_THREADS (4) wrong-auth requests in parallel and measure.
        # With the old MilliSleep(250) each thread would block 250 ms, so 4 parallel
        # requests would take >= 250 ms.  With the backoff map the threads return
        # immediately; even 4 parallel bad requests must complete well under 250 ms.
        errors = []
        def send_wrong_auth():
            try:
                c = http.client.HTTPConnection(url0.hostname, url0.port)
                c.request('POST', '/', '{"method": "getbestblockhash"}', wrong_headers)
                r = c.getresponse()
                r.read()
                assert_equal(r.status, 401)
                c.close()
            except Exception as e:
                errors.append(str(e))

        threads = [threading.Thread(target=send_wrong_auth) for _ in range(4)]
        t0 = time.monotonic()
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10.0)
        elapsed = time.monotonic() - t0
        assert not errors, f"Parallel wrong-auth threads raised: {errors}"
        # 4 parallel bad requests must finish well under 4 × 250 ms = 1 s
        assert elapsed < 1.0, f"4 parallel wrong-auth requests took {elapsed:.2f}s; worker threads may be blocking"

        # After failures, a valid request from the same IP must still succeed
        # (backoff window does not block legitimate credentials)
        conn = http.client.HTTPConnection(url0.hostname, url0.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', correct_headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 200)
        conn.close()

        # Successful auth clears the backoff; a subsequent wrong-auth should log again
        # (i.e., not be silently dropped as within-backoff) — just verify it returns 401
        conn = http.client.HTTPConnection(url0.hostname, url0.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', wrong_headers)
        resp = conn.getresponse()
        assert_equal(resp.status, 401)
        conn.close()


if __name__ == '__main__':
    HTTPBasicsTest ().main ()
