# RaiBinary(Simple) 設計仕様

## 1. 用語定義
|用語|定義|
|--|--|
|セクションディレクトリ|ファイル中の各セクションの位置(offset)と長さ(size)を管理する索引|
|チャンク|独立して読み込めるデータ単位。複数レコードを含む|
|layoutId|object のキー並びと型並びを表す定義 ID|
|layoutGroup|同じ layoutId を持つレコード群|
|columnData|フィールドごとに縦持ち配置した実データ領域|

## 2. ファイル全体レイアウト
注記: マジックとバージョンはファイルヘッダの一部であり、重複定義ではない。

|順序|領域|説明|
|--:|--|--|
|1|ファイルヘッダ|magic/version を含む固定先頭領域|
|2|セクションディレクトリ|後続セクションの配置情報|
|3|キー文字列テーブル|object キー辞書|
|4|レイアウト定義テーブル|layoutId 定義群|
|5|チャンクディレクトリ|各チャンクの位置情報|
|6|データチャンク列|実レコードデータ|

## 3. ファイルヘッダ
|バイト数|項目|意味|
|--:|--|--|
|4|magic|"RAIS" を 1bit 左シフトした固定値 (0xA4 82 92 A6)|
|1|version|形式バージョン (現行 0x02)|
|1|flags|予約フラグ|
|2|headerSize|ヘッダ全体長|
|4|sectionCount|セクション数|
|8|recordCount|ファイル全体のレコード数|

## 4. セクションディレクトリ
### 4.1 エントリ書式
|バイト数|項目|意味|
|--:|--|--|
|2|sectionType|セクション種別|
|8|offset|ファイル先頭からの絶対位置|
|8|size|セクション長|

### 4.2 sectionType
|値|意味|
|--:|--|
|1|keyStringTable|
|2|layoutDefinitionTable|
|3|chunkDirectory|
|4|chunkData|

### 4.3 配置例
|index|sectionType|offset|size|
|--:|--|--:|--:|
|0|1|128|96|
|1|2|224|160|
|2|3|384|64|
|3|4|448|4096|

## 5. キー文字列テーブル
### 5.1 書式
|項目|形式|
|--|--|
|entry|length(u32) + utf8Bytes|
|参照|keyId(u32)|

### 5.2 採用理由
|方針|理由|
|--|--|
|object キーのみ辞書化|重複率が高く、辞書化効果が安定|
|値文字列全体は必須辞書化しない|高カーディナリティでランダム参照増、辞書構築コスト増|

## 6. レイアウト定義テーブル
### 6.1 エントリ書式
|項目|形式|意味|
|--|--|--|
|fieldCount|u32|フィールド数|
|keyId|u32|キー辞書参照|
|valueType|u8|型タグ (bit7 は nullable フラグ)|

### 6.2 valueType 一覧
|値|型|
|--:|--|
|0x00|null|
|0x01|bool|
|0x02|uint8|
|0x03|uint16|
|0x04|uint32|
|0x05|uint64|
|0x06|int8|
|0x07|int16|
|0x08|int32|
|0x09|int64|
|0x0A|float32|
|0x0B|float64|
|0x10|string|
|0x11|array|
|0x12|object|

valueType のビット割り当て:
- bit7: nullable フラグ (0=非null許容, 1=null許容)
- bit0-6: 基本型 (上表)

例:
- `uint8_t` は `0x02`
- `optional<uint8_t>` は `0x82`

## 7. チャンクディレクトリ
チャンクディレクトリは、各データチャンクの位置と長さを管理する。

|バイト数|項目|意味|
|--:|--|--|
|4|chunkCount|チャンク数|
|繰返し|chunkOffset(u64), chunkSize(u32)|各チャンクの位置情報|

## 8. データチャンク
### 8.1 チャンクヘッダ
|バイト数|項目|意味|
|--:|--|--|
|4|chunkSize|このチャンク全体長|
|4|recordCount|チャンク内レコード数|
|4|layoutGroupCount|layoutGroup 数|

### 8.2 layoutGroup
|項目|形式|意味|
|--|--|--|
|layoutId|u32|どのレイアウト定義を使うか|
|recordCount|u32|この group のレコード数|
|columnDirectoryCount|u32|列数|
|columnDirectory|配列|列ごとの位置情報|
|columnData|byte列|列本体|

### 8.3 columnDirectory
|項目|形式|意味|
|--|--|--|
|fieldIndex|u32|レイアウト内の何番目のフィールドか|
|encodingType|u8|列エンコード種別|
|dataOffset|u32|columnData 先頭からの相対位置|
|dataSize|u32|この列データ長|

### 8.4 encodingType
|値|意味|
|--:|--|
|1|fixedWidthPrimitive|
|2|utf8StringOffsetAndBytes|
|3|nestedArrayOffsetAndData|
|4|nestedObjectOffsetAndData|

### 8.5 columnDirectory 例
|fieldIndex|encodingType|dataOffset|dataSize|
|--:|--:|--:|--:|
|0|1|0|9|
|1|2|9|19|
|2|1|28|9|

## 9. 列データ書式
### 9.1 fixedWidthPrimitive
|要素|意味|
|--|--|
|nullBitmap|nullable フィールドの場合のみ出力する null 判定ビット列|
|valueBuffer|固定幅値の連続領域|

### 9.2 utf8StringOffsetAndBytes
|要素|意味|
|--|--|
|nullBitmap|nullable フィールドの場合のみ出力する null 判定ビット列|
|offsetBuffer|各文字列の開始位置 (要素数+1)|
|utf8Bytes|文字列本体連続領域|

### 9.3 例
|列|内容|
|--|--|
|int32(id)|nullBitmap=0b00000011, valueBuffer=01 00 00 00 02 00 00 00|
|string(name)|nullBitmap=0b00000011, offsets=[0,1,3], utf8Bytes=41 42 43|
|float32(score)|nullBitmap=0b00000011, valueBuffer=00 00 28 41 00 00 A0 41|

### 9.4 fixedWidthPrimitive を使う JSON 例
対象 JSON:
[
  {"hp": 120, "mp": 10},
  {"hp": 100, "mp": null},
  {"hp": 80,  "mp": 5}
]

前提となるフィールド型:
|フィールド|型|valueType|
|--|--|--:|
|hp|uint8_t|0x02|
|mp|optional<uint8_t>|0x82|

列データ:
|列|内容|
|--|--|
|hp (non-nullable)|valueBuffer=78 64 50 (nullBitmap なし)|
|mp (nullable)|nullBitmap=0b00000101, valueBuffer=0A 00 05|

注記:
- `optional<T>` は nullable フラグ(bit7=1)により判定する。
- non-nullable フィールドは nullBitmap を出力しない。

### 9.5 nestedObjectOffsetAndData を使う JSON 例
対象 JSON:
[
  {"id": 1, "profile": {"level": 10, "rank": "A"}},
  {"id": 2, "profile": {"level": 12, "rank": "B"}},
  {"id": 3, "profile": null}
]

前提となるフィールド型:
|フィールド|型|valueType|
|--|--|--:|
|id|uint32_t|0x04|
|profile|optional＜object＞|0x92|

親 layout の列データ:
|列|内容|
|--|--|
|id|valueBuffer=01 00 00 00 02 00 00 00 03 00 00 00|
|profile|nullBitmap=0b00000011, offsetBuffer=[0, 12, 24, 24], nestedChunkLikeData=(下表)|

profile(object) の nestedChunkLikeData:
|項目|内容|
|--|--|
|nested layoutId|21 (fields: level(uint8), rank(string))|
|level 列|valueBuffer=0A 0C|
|rank 列|offsetBuffer=[0,1,2], utf8Bytes=41 42|

注記:
- `offsetBuffer` は profile の各要素が nestedChunkLikeData 内で始まる位置を示す。
- 3件目 profile は null のため、`offsetBuffer[2] == offsetBuffer[3]` となる。

## 10. JSON 相互変換
|項目|方針|
|--|--|
|object/array|string/number/bool/null|対応する論理型へ相互変換|
|整数幅|JSON 入力時は値域から最小幅へ正規化|
|浮動幅|必要に応じ float32/float64 を保持|

## 11. 並列化と SIMD
|観点|設計|
|--|--|
|読込並列|チャンク単位で静的分配|
|書込並列|スレッドごとにローカルチャンク作成後に連結|
|SIMD|nullBitmap 走査、固定幅列処理、文字列境界処理に適用|

## 12. 妥当性チェック
|項目|検証|
|--|--|
|offset/size|範囲内・非重複|
|dataOffset/dataSize|columnData 範囲内|
|未知タイプ|sectionType/encodingType/valueType はエラー|
|入力不足|失敗として返却、部分復元しない|
