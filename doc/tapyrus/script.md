Tapyrus Script Specification
============================

Script Evaluation
--------------------
Tapyrus scripts are evaluated with the following rules.

- *Default* are _Mandatory_ and consensus critical rules in Tapyrus. Scripts are always evaluated using these rules. But there is no flag controlling them. These rules can never be disabled. When script evaluation fails, the transaction is invalid and the peer connection is dropped.
- *Standard* rules have a flag definition in Tapyrus. Standard Tapyrus scripts need to satisfy these flags. When script evaluation fails with these flags, the transaction is invalid and the peer connection is dropped.
- *Standard Non-Mandatory* flags are defined with the value in the table below. When script evaluation fails with these flags, the transaction is invalid. But the peer connection is _NOT_ dropped.
- *Non-Standard* are unused flags. Currently Witness scripts are non-standard in tapyrus.

| Flag | Value | Description  | Usage |
| :---: | :---: | :---: | :---: | 
|SCRIPT_VERIFY_NONE| 0 | | |
|SCRIPT_VERIFY_P2SH | - |Evaluate P2SH subscripts | Default |
|SCRIPT_VERIFY_STRICTENC | - |Passing a non-strict-DER signature or one with undefined hashtype to a checksig/checkdatasig operation causes script failure. Evaluating a pubkey that is not (0x04 + 64 bytes) or (0x02 or 0x03 + 32 bytes) by checksig/checkdatasig causes script failure. | Default |
|SCRIPT_VERIFY_DERSIG | - |Passing a non-strict-DER signature to a checksig operation causes script failure| Default|
|SCRIPT_VERIFY_LOW_S | - |Passing a non-strict-DER signature or one with S > order/2 to a checksig operation causes script failure | Default|
|SCRIPT_VERIFY_NULLDUMMY | - |Verify dummy stack item consumed by CHECKMULTISIG is of zero-length| Default|
|SCRIPT_VERIFY_MINIMALDATA | - |Require minimal encodings for all push operations. Whenever a stack element is interpreted as a number, it must be of minimal length  | Default |
|SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | - |Verify CHECKLOCKTIMEVERIFY. BIP65| Default |
|SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | - |Support CHECKSEQUENCEVERIFY . BIP112 | Default |
|SCRIPT_VERIFY_SIGPUSHONLY | (1U << 0) |Using a non-push operator in the scriptSig causes script failure | Standard Non-Mandatory |
|SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS | (1U << 1) |Discourage use of NOPs reserved for upgrades | Standard |
|SCRIPT_VERIFY_CLEANSTACK | (1U << 2)|Require that only a single stack element remains after evaluation | Standard |
|SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM | (1U << 12)|Making v1-v16 witness program non-standard| Standard |
|SCRIPT_VERIFY_MINIMALIF | (1U << 13)|Segwit script only: Require the argument of OP_IF/NOTIF to be exactly 0x01 or empty vector| Standard |
|SCRIPT_VERIFY_NULLFAIL | (1U << 14)|Signature(s) must be empty vector if a CHECK(MULTI)SIG operation failed| Standard |
|SCRIPT_VERIFY_WITNESS_PUBKEYTYPE | (1U << 15)|Public keys in segregated witness scripts must be compressed| Standard |
|SCRIPT_VERIFY_CONST_SCRIPTCODE | (1U << 16)|Making OP_CODESEPARATOR and FindAndDelete fail any non-segwit scripts| Standard |
|SCRIPT_VERIFY_WITNESS | (1U << 11)|Support segregated witness| Non-Standard |

New Script OP_Codes
--------------------
Tapyrus adds the following OP_codes in addition to those available in bitcoin. There is no change in the behaviour of old Op codes.

| Op Code | Value | Inputs (In the order they are pushed into the stack) | Description  |
| :---: | :---: | :---: | :---: |
|OP_CHECKDATASIG| 0xba |  Public Key, Message, Signature | Used to add a signed message and signature to script that be publicly verified.|
|OP_CHECKDATASIGVERIFY| 0xbb | Public Key, Message, Signature | Used to add a signed message and signature to script that be publicly verified.|