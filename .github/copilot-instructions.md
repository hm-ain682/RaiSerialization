# 指示に対する対応方針
- 指示された条件は**必ず守る**こと
- 指示された内容を適切に対処できない場合は、質問すること

# 命名規約
- 名前の構成要素として略語は禁止（以下は例外）
  - 一般に略語が用いられるものは許容（例：JSON、HTML、URL、IDなど）
  - 他ライブラリ等の用語
  - 短い範囲で用いられるローカル変数（ループ変数など）
- 型：PascalCase（例：`MyClass`）
- 関数：camelCase（例：`myFunction`）
- 非publicメンバー変数：camelCase+_（例：`myPrivateVariable_`）
- publicメンバー変数：camelCase（例：`myPublicVariable`）
- ローカル変数：camelCase（例：`myLocalVariable`）
- 定数（プリプロセッサによるもののみ、constexprは除く）：_区切りの大文字（例：`MY_CONSTANT`）
- 名前空間：snake_case（例：`my_namespace_`）
- モジュール：snake_case（例：`my_module`）
- hやcppのファイル：PascalCase+拡張子（例：`MyClass.h`、`MyClass.cpp`）
- cppmのファイル：モジュール名+拡張子（例：`category.my_module.cppm`）
- テストファイル：PascalCase+Test+拡張子（例：`MyClassTest.cpp`）
- ディレクトリ：PascalCase（例：`MyModule`）

# 実装規約
- 1行の長さは基本的に100文字まで
- `{`の後で改行
- if、for、whileなどの制御構文の後には必ず`{}`を付与
- 1つの関数は最大100行まで

# コメントの記述ルール
- 日本語で記述
- 型定義、関数定義では、doxygen形式で記述
- 関数定義のコメントは、関数が何をするかを説明する
- 型定義のコメントは、型が何の役割を持つかを説明する
- 型定義、関数定義、メンバー変数には**必ず**コメントを付与する

## 関数実装内のコメント
- そのように実装した理由を記述する
  例：「バッファが空であることは別で保証されているため」
- 処理の説明をする必要は基本的にはないが、以下では適切なコメントが必要
  - 長いコードブロック：処理の概要を一言で説明する
  - 条件分岐の各分岐：どういうときにそこに該当するか記述する
