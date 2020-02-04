Network ID and Magic Bytes
===================

Network ID is identifier of the Tapyrus network(prod, dev, paradium).

Magic bytes are used as a way to identify the separate network messages sent between nodes on the Tapyrus network.

For example, if you are trying to connect to a node with your own code, every message you send to that node should begin with `00F0FF01`, and every message you receive from that node will start with the same magic bytes too.

List of Network IDs / Magic bytes
===================

| Network ID | Tapyrus Mode | Magic Bytes | Description    |
| ---------- | ------------ | ----------- | -----------    |
| 1          | prod         | 0x00F0FF01  | Production     |
| 101        | prod         | 0x64F0FF01  | Paradium       |
| 721        | prod         | 0xD0F2FF01  | HAW            |
| 1939510133 | prod         | 0x74839A75  | Public Testnet |
| 1905960821 | dev          | 0x74979A73  | Regtest        |

Calculation of the Magic Bytes
===================

Magic bytes can be derived from Network ID.

The formula to calculate magic bytes is following:

```
(Magic Bytes) = 33550335 + (Network ID)
```

