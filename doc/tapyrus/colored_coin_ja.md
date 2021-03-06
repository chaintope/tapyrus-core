Tapyrus上でTPC（tapyrus）以外のトークンをサポートするための仕様について記述する。

# イントロダクション

Tapyrusで任意のトークンの発行・送付・破棄を可能にするため、新しいopcode`OP_COLOR`(0xbc)を導入する。
`OP_COLOR`は、以前BCHで導入が検討されていた`OP_GROUP`をベースとし、いくつかの改良を加えたもので、以下の機能を可能にする。

* トークンの発行
   * 再発行可能なトークンの発行
   * 再発行不可能なトークンの発行
   * NFTの発行
* トークンの送付
* トークンの焼却

# 仕様

スクリプト内に`OP_COLOR` opcodeが現れると、スタックの一番上の要素がCOLOR識別子として使われる。
スタックに１つも要素が残っていない場合、もしくはCOLOR識別子が後述するルールに従っていない場合、スクリプトは失敗する。
COLOR識別子がルールに準拠する場合、そのUTXOのコインはそのCOLOR識別子のコインの量を表す。
`OP_COLOR`の含まれないScriptはデフォルトトークンTPC（tapyrus）の量を指す。

トランザクションにおいて、インプットのカラー識別子が持つトークンの総量と、アウトプットのカラー識別子のトークンの総量は一致しなければならない。
つまりトランザクション内の各トークンのバランスを維持する。尚、現段階ではSignerがブロック生成の手数料としてトークンを受け取ることはないものとする。

またScript内で`OP_COLOR`を使用する場合、以下の制約が適用される。

* Script内に含められる`OP_COLOR`の数は１つのみ。
* `OP_COLOR`は`OP_IF`などの制御opcode内に記述することはできない。

上記のカウントと制約のため、各アウトプットのカスタムトークンのscriptPubkeyには必ず`OP_COLOR`が含まれる。
このため、P2SHのredeem scriptに`OP_COLOR`を含めることはできない。そのようなスクリプトを構成した場合、
スクリプトインタプリタのスタックに複数の`OP_COLOR`が含まれることになり、P2SHを使用する際に必ずエラーとなりコインの喪失に繋がる。

結果、既存のP2PKHとP2SHをカラーリングした以下のタイプのscriptPubkeyをサポートする。

* `CP2PKH`(Colored P2PKH)：  
`<COLOR identifier> OP_COLOR OP_DUP OP_HASH160 <H(pubkey)> OP_EQUALVERIFY OP_CHECKSIG`
* `CP2SH`(Colored P2SH)：  
`<COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL`

Tapyrus Coreではトランザクションの標準ルールで任意のスクリプトを許可しているため、上記以外の任意のスクリプトで`OP_COLOR`が利用可能だ。

## COLOR識別子

COLOR識別子は1バイトの`TYPE`と、32バイトの`PAYLOAD`で構成されます。

COLOR識別子が現在サポートするタイプと、対応するペイロードは以下の通り。

TYPE|定義|PAYLOAD
---|---|---
0xC1|再発行可能なトークン|発行インプットのscriptPubkeyのSHA256値である32バイトのデータ
0xC2|再発行不可能なトークン|発行インプットのOutPointのSHA256値である32バイトのデータ
0xC3|NFT|発行インプットのOutPointのSHA256値である32バイトのデータ

トークンの発行Txでは、アウトプットの`OP_COLOR`が指定するCOLOR識別子と、インプットが上記ルールを満たしているか検証する。
さらにタイプ`0xC3`の場合、追加で、発行量が1であることを検証しなければならない。

## トークンの発行

トークンを新規に発行する場合、トークン発行用のUTXOをインプットにセットし、そのUTXOから上記のルールをベースにCOLOR識別子を導出する。
そのCOLOR識別子と`OP_COLOR` opcodeを使用したscriptPubkey(CP2PKH, CP2SH, etc)をトランザクションアウトプットにセットしたトランザクションを作成する。

この時、新規トークン発行のアウトプットには任意のトークンの量を`value`をセットできる。
また、インプットで指定したUTXOのTPCは別の非トークンアウトプットを作成し、回収する。`

## トークンの送付

トークンを送付する場合は、トークンを持つUTXOをインプットにしたトランザクションを作成し、
送金先のアドレスに対して、インプットのトークンと同じCOLOR識別子と`OP_COLOR` opcodeを付与したアウトプットを追加する。
この時、インプットの数やアウトプットの数、トークンの種類は複数設定でき、インプットとアウトプットでトークンの種類毎の総量が保持されていれば問題ない。

## トークンの焼却

トークンを焼却する場合は、焼却するトークンを持つUTXOと手数料用のTPCを持つUTXOをインプットにしたトランザクションを作成し、
手数料を差し引いたTPCのお釣りを受け取るアウトプットを追加する。トークンのUTXOのvalue値は全てトークンの量であるため、
手数料を設定するためには必ず、TPCのUTXOをセットする必要がある。

また、上記３つのトークン処理を１つのトランザクションを組み合わせることは可能である。

各組み合わせおよび有効/無効のパターンについて、以下の資料に掲載する:

* https://docs.google.com/spreadsheets/d/1hYEe5YVz5NiMzBD2cTYLWdOEPVUkTmVIRp8ytypdr2g/

## アドレス

CP2PKHおよびCP2SH用に新しくアドレスフォーマットを定義します。

### バージョンバイト

アドレス生成の際に使用するバージョンバイトは以下のとおりです。

タイプ|prod|dev
---|---|---
CP2PKH|0x01|0x70
CP2SH|0x06|0xc5

### ペイロード

P2PKHやP2SHはそれぞれ公開鍵およびredeem scriptのRIPEMD-160ハッシュ値をアドレスのペイロードとしていましたが、CP2PKHおよびCP2SHのアドレスは、以下のデータで構成されます(`|`はデータの連携を表します)。

* CP2PKH: `<Color識別子（33バイト）>|<公開鍵ハッシュ（20バイト）>`
* CP2SH: `<Color識別子（33バイト）>|<スクリプトハッシュ（20バイト）>`

いずれもペイロードのデータ長は53バイトになります。

### 注意事項

CP2PKHアドレスおよびCP2SHアドレスは、P2PKHアドレス、P2SHアドレスと違ってColor識別子を知らないと作成することはできません。ただ、カラードコインを受信したいユーザーが識別子を知っているとも限らないため、コインを受信するユーザーはP2PKHアドレスやP2SHアドレスを提示し、送信者がそれにカラー識別子を付与して送信することが十分考えられます。そのため、CP2PKHアドレスやCP2SHアドレスは自身が保有するウォレットでのみ使われるということが考えれます。

# 懸念事項

* OP_COLORの影響範囲はスクリプト内に閉じず、量のチェックという意味でトランザクションインプット/アウトプットに影響が派生する。

# 参考
* [OP_GROUP](https://github.com/gandrewstone/BitcoinUnlimited/blob/238ca764385f94a4c371e61424e3307d7da9eb56/doc/opgroup-tokens.md)
* [On Representative Tokens (Colored Coins)](https://www.yours.org/content/on-representative-tokens--colored-coins--bb7a829b965c/)
* [Response to OP_GROUP Criticism](https://www.yours.org/content/response-to-op_group-criticism-d088a7f1e6ad)