# RaiBinary(Simple) 要件仕様

## 1. 目的
RaiBinary(Simple) は、スキーマ事前定義なしで JSON 互換データを
永続化するためのバイナリ形式である。

## 2. 対象範囲
|区分|内容|
|--|--|
|対象|JSON 互換データの保存・読込、性能要件、互換性要件|
|非対象|圧縮、暗号化、署名、任意精度数値|

## 3. 機能要件
|ID|要件|必須|JSON対比例|
|--|--|--|--|
|FR-01|スキーマ事前定義なしで保存・読込できる|Yes|JSON: キー増減が自由 / RB: レイアウトをファイル内で自動定義|
|FR-02|JSON と概ね相互変換できる|Yes|JSON: object/array/string/number / RB: 同等の論理型を保持|
|FR-03|文字列は UTF-8 で保存する|Yes|JSON: UTF-8 テキスト / RB: UTF-8 バイト列|
|FR-04|整数型は 8/16/32/64 ビットを扱う|Yes|JSON: number 1 / RB: int8〜int64, uint8〜uint64|
|FR-05|浮動小数点は float32/float64 を扱う|Yes|JSON: number 10.5 / RB: float32 または float64|
|FR-06|JSON テキストそのものの保存は行わない|Yes|JSON: "{\"id\":1}" / RB: 構造化バイナリ|
|FR-07|複数チャンクの独立読込を可能にする|Yes|JSON: 全文パース / RB: 必要チャンクのみ読込|
|FR-08|object キーは辞書化し keyId 参照する|Yes|JSON: "id" を毎回文字列保存 / RB: keyId を再利用|
|FR-09|値文字列辞書は任意オプションとする|No|JSON: 値を都度文字列保存 / RB: 条件一致時のみ辞書化|

## 4. 性能要件
比較対象は protobuf の Struct 系表現とする。

|ID|指標|条件|合格基準|
|--|--|--|--|
|PR-01|Read throughput|4 threads|protobuf 比 1.30 倍以上|
|PR-02|Write throughput|4 threads|protobuf 比 1.20 倍以上|
|PR-03|Read throughput|1 thread|protobuf 比 1.10 倍以上|
|PR-04|Write throughput|1 thread|protobuf 比 1.05 倍以上|
|PR-05|出力サイズ|主要データセット|JSON より小さいケース比率を過半超|

注記:
- 固定スキーマ + 生成コード protobuf は参考比較とし、合否判定から除外する。

## 5. ベンチマーク要件
|項目|要件|
|--|--|
|データ規模|小・中・大|
|データ種類|数値中心、文字列中心、混在|
|指標|read MB/s、write MB/s、p50/p95 latency、出力サイズ|
|スレッド条件|1 thread、4 threads|
|判定|PR-01〜PR-05 を満たすこと|

## 6. 互換性要件
|ID|要件|必須|
|--|--|--|
|CR-01|ファイル先頭 version で互換判定する|Yes|
|CR-02|未知 sectionType はエラー|Yes|
|CR-03|未知 encodingType はエラー|Yes|
|CR-04|未知 valueType はエラー|Yes|

## 7. エラーハンドリング要件
|ID|条件|動作|
|--|--|--|
|ER-01|入力不足|失敗を返す|
|ER-02|長さ不正|失敗を返す|
|ER-03|オフセット不正(逆転/範囲外)|失敗を返す|
|ER-04|型不正|失敗を返す|
|ER-05|部分復元の要求|許可しない|

## 8. JSON との相互運用上の注意
|項目|内容|
|--|--|
|整数幅|JSON では int32/int64 の見分けがつかない場合がある|
|浮動幅|JSON では float32/float64 の見分けがつかない場合がある|
|方針|JSON 入力時は値域に応じて最小幅へ正規化|
