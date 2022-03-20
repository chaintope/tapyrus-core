Tx Generator
------------

Tx Generator is python process based on functional test framework that can be used to populate a test network/node, generate wallet to measure the effect of utxo:block ratio on load time, cost of spending half the wallet etc

Options
--------

    *--maxBlockCount* : stop the block and generation when the number of blocks reaches this count.
    *--maxUtxoCount* : stop the transaction generation when the number of utxos in the wallet reaches this count
    *--daemon* : option to set the daemon mode i.e. to start a node in any network and populate its blocks with transactions. the node's wallet spends all its utxos
    *--daemon_datadir* : data directory for daemon mode. default is -"~/.tapyrus/tapyrus-testnet"-
    *--daemon_genesis* : genesis block hex for daemon mode. default is the testnet genesis block

To run
-------

1. Spend wallet
    $ python /test/functional/txgenerator/txgenerator.py
            --tracerpc
            --nocleanup

2. Generate wallet
    $ python /test/functional/txgenerator/txgenerator.py
            --tracerpc
            --nocleanup
            --maxBlockCount
            --maxUtxoCount

3. Populate a node
    $ python /test/functional/txgenerator/txgenerator.py
            --tracerpc
            --nocleanup
            --daemon

RPC Trace
----------

RPC trace is generated in the tmpdir after from the test framework log. It contains the RPC call id, rpc name and elapsed time as follows:

64                   createrawtransaction 0.001198
65           signrawtransactionwithwallet 0.001889
66                     sendrawtransaction 0.001697
67                            listunspent 1.712024
68                          getnewaddress 0.261340
69                   createrawtransaction 0.001576


TODO
----

1. Expand transaction template - add colored coins, signature type, invalid tx
2. Test with testnet node