Network ID and Magic Bytes
===================

Network ID is identifier of the Tapyrus network(mainnet, testnet, regtest, paradium).

Magic bytes are used as a way to identify the separate network messages sent between nodes on the Tapyrus network.

For example, if you are trying to connect to a node with your own code, every message you send to that node should begin with `00F0FF01`, and every message you receive from that node will start with the same magic bytes too.

List of Network IDs / Magic bytes
===================

| Name        | Network ID | Magic Bytes |
| ----------- | ---------- | ----------- |
| Mainnet     | 1          | 0x00F0FF01  |
| Paradium    | 101        | 0x64F0FF01  |
| HAW         | 721        | 0xD0F2FF01  |
| Testnet     | 1939510133 | 0x74839A75  |
| Regtest     | 1905960821 | 0x74979A73  |

Calculation of the Magic Bytes
===================

Magic bytes can be derived from Network ID.

The formula to calculate magic bytes is following:

```
(Magic Bytes) = 33550335 + (Network ID)
```

