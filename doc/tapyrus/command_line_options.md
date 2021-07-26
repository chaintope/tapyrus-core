These new command line options are available in Tapyrus

|Option |Definition  | Example |
| :---: | :--- | :--- |
| <h3> datacarriermultiple <h3>| Traditionally datacarrier transactions allowed one output with OP_RETURN per transaction. But Tapyrus can allow more than one output with one OP_RETURN. This behaviour is controlled by __datacarriermultiple__. Its default value is false. When it is set to true Tapyrus node accepts transactions with more than one output having OP_RETURN as standard transaction and relays it to its peers. <dl> <dd> -  More than one OP_RETURN in one output script is not accepted. <br /> -  Having OP_RETURN as the data part of another OP_RETURN is not accepted.</dl>| When datacarriermultiple is present in commandline, a transaction with the following two outputs is considered standard: <h5><br /><br />`vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("0100");` <br /> `  vout[1].scriptPubKey = CScript() << OP_RETURN << ParseHex("00");` |
