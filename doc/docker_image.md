# Docker Image for Tapyrus Core

## Quick Start

### Testnet

Refer to [How to start a node on Tapyrus testnet?](https://github.com/chaintope/tapyrus-core/blob/master/doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet)

#### Create `tapyrus.conf`

```bash
cat << EOS > tapyrus.conf
networkid=1939510133
txindex=1
server=1
rest=1
rpcuser=rpcuser
rpcpassword=rpcpassword
rpcbind=0.0.0.0
rpcallowip=127.0.0.1
addseeder=static-seed.tapyrus.dev.chaintope.com
EOS
```

#### Running tapyrus node

```bash
$ docker run -d --name 'tapyrus_node_testnet' -v $PWD/tapyrus.conf:/etc/tapyrus/tapyrus.conf -e GENESIS_BLOCK_WITH_SIG='01000000000000000000000000000000000000000000000000000000000000000000000044cc181bd0e95c5b999a13d1fc0d193fa8223af97511ad2098217555a841b3518f18ec2536f0bb9d6d4834fcc712e9563840fe9f089db9e8fe890bffb82165849f52ba5e01210366262690cbdf648132ce0c088962c6361112582364ede120f3780ab73438fc4b402b1ed9996920f57a425f6f9797557c0e73d0c9fbafdebcaa796b136e0946ffa98d928f8130b6a572f83da39530b13784eeb7007465b673aa95091619e7ee208501010000000100000000000000000000000000000000000000000000000000000000000000000000000000ffffffff0100f2052a010000002776a92231415132437447336a686f37385372457a4b6533766636647863456b4a74356e7a4188ac00000000' tapyrus/tapyrusd:latest

c8bdf7809142f149bd5e510bb6f255d49b1257d4141ced01ea8737a8530643c2
```

```bash
$ docker exec tapyrus_node_testnet tapyrus-cli -conf=/etc/tapyrus/tapyrus.conf getblockchaininfo

{
  "chain": "1939510133",
  "mode": "prod",
  "blocks": 9639,
  "headers": 9639,
  "bestblockhash": "ca526d81cffea666e9cfc8442f260e1e136bf4f1449f4a088986a9e5713e17ea",
  "mediantime": 1593073737,
  "verificationprogress": 1,
  "initialblockdownload": false,
  "size_on_disk": 2989015,
  "pruned": false,
  "aggregatePubkeys": [
    {
      "0366262690cbdf648132ce0c088962c6361112582364ede120f3780ab73438fc4b": 0
    }
  ],
  "warnings": ""
}
```

### Dev Mode

Refer to [How to start tapyrus in dev mode?](https://github.com/chaintope/tapyrus-core/blob/master/doc/tapyrus/getting_started.md#how-to-start-tapyrus-in-dev-mode)

By default, `tapyrus.conf` for dev mode is generated inside container.

#### Generate genesis block

```bash
$ docker run tapyrus/tapyrusd:latest tapyrus-genesis -dev -signblockpubkey=03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d -signblockprivatekey=cUJN5RVzYWFoeY8rUztd47jzXCu1p57Ay8V7pqCzsBD3PEXN7Dd4

0100000000000000000000000000000000000000000000000000000000000000000000002b5331139c6bc8646bb4e5737c51378133f70b9712b75548cb3c05f9188670e7440d295e7300c5640730c4634402a3e66fb5d921f76b48d8972a484cc0361e66ef74f45e012103af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d40e05f064662d6b9acf65ae416379d82e11a9b78cdeb3a316d1057cd2780e3727f70a61f901d10acbe349cd11e04aa6b4351e782c44670aefbe138e99a5ce75ace01010000000100000000000000000000000000000000000000000000000000000000000000000000000000ffffffff0100f2052a010000001976a91445d405b9ed450fec89044f9b7a99a4ef6fe2cd3f88ac00000000
```

#### Running tapyrus node

```bash
$ docker run -d --name 'tapyrus_node_dev' -e GENESIS_BLOCK_WITH_SIG='0100000000000000000000000000000000000000000000000000000000000000000000002b5331139c6bc8646bb4e5737c51378133f70b9712b75548cb3c05f9188670e7440d295e7300c5640730c4634402a3e66fb5d921f76b48d8972a484cc0361e66ef74f45e012103af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d40e05f064662d6b9acf65ae416379d82e11a9b78cdeb3a316d1057cd2780e3727f70a61f901d10acbe349cd11e04aa6b4351e782c44670aefbe138e99a5ce75ace01010000000100000000000000000000000000000000000000000000000000000000000000000000000000ffffffff0100f2052a010000001976a91445d405b9ed450fec89044f9b7a99a4ef6fe2cd3f88ac00000000' tapyrus/tapyrusd:latest

15720f9a65ec5fecc71192c156a18d1045902277256883ec9222a5b03c26f1cb
```

```bash
$ docker exec tapyrus_node_dev tapyrus-cli -conf=/etc/tapyrus/tapyrus.conf getblockchaininfo

{
  "chain": "1905960821",
  "mode": "dev",
  "blocks": 0,
  "headers": 0,
  "bestblockhash": "ba474220e31b118a3f037827c0b6517ef22071c9db8f23152c873545c8c87fec",
  "mediantime": 1563342688,
  "verificationprogress": 1,
  "initialblockdownload": true,
  "size_on_disk": 312,
  "pruned": false,
  "aggregatePubkeys": [
    {
      "03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d": 0
    }
  ],
  "warnings": ""
}
```

#### Generate new block

*aggregate private key* is `c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d3`

```bash
$ docker exec tapyrus_node_dev tapyrus-cli -conf=/etc/tapyrus/tapyrus.conf getnewaddress

mhQYgFmHJCNSjDpTeVhEYPyMd7bvwpJYKU

$ docker exec tapyrus_node_dev tapyrus-cli -conf=/etc/tapyrus/tapyrus.conf generatetoaddress 1 "mhQYgFmHJCNSjDpTeVhEYPyMd7bvwpJYKU" "c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d3"

[
  "a1efcc962c665b9b87899a669ef3a6a08eaad88fef113c1499db12e109fdda08"
]

$ docker exec tapyrus_node_dev tapyrus-cli -conf=/etc/tapyrus/tapyrus.conf listunspent

[
  {
    "txid": "a6535fde2407439fac5112f8f49acefb92b20e6682db7546346a1fa2bbd98b5b",
    "vout": 0,
    "address": "mhQYgFmHJCNSjDpTeVhEYPyMd7bvwpJYKU",
    "label": "",
    "scriptPubKey": "76a91414ba0a87e031317061db0ddada3397b8ab7d2cab88ac",
    "amount": 50.00000000,
    "confirmations": 1,
    "spendable": true,
    "solvable": true,
    "safe": true
  }
]
```

## Using a cusotm Tapyrus configuration file

The default configuration for tapyrus-core can be found in `/etc/tapyrus/tapyrus.conf`.

If `/my/tapyrus.conf` is the path and name of your custom configuration file, you can start your tapyrus-core container like this:

```bash
$ docker run -d -v /my/tapyrus.conf:/etc/tapyrus/tapyrus.conf -e GENESIS_BLOCK_WITH_SIG='00000...00000' tapyrus/tapyrusd:latest
```


## Environment Variables

### `GENESIS_BLOCK_WITH_SIG`

This variable is required when running `tapyrusd`.

### `DATA_DIR`

This variable is optional, By default, this value is `/var/lib/tapyrus`

### `CONF_DIR`

This variable is optional, By default, this value is `/etc/tapyrus`

## Where to Store Data

1. Create a data directory on a suitable volume on your host system, e.g. `/my/own/datadir`.
2. Start your `tapyrus-core` container like this:

```bash
$ docker run -v /my/own/datadir:/var/lib/tapyrus -e GENESIS_BLOCK_WITH_SIG='00000...00000' -d tapyrus-core:tag
```

The `-v /my/own/datadir:/var/lib/tapyrus` part of the command mounts the `/my/own/datadir` directory from the underlying host system as `/var/lib/tapyrus` inside the container, where tapyrus-core by default will write its data files.
