# RaiJson

RaiJson is a C++23 JSON5 toolkit built with C++ modules. It provides:
- Zero-dependency JSON5 tokenizer, parser, and writer
- Declarative field binding (`JsonField`, `JsonEnumField`, `JsonPolymorphicField`) for struct ↔ JSON conversion
- Polymorphic object support (single object and arrays) with type tags
- Small, fixed-capacity sorted hash map for fast key lookup in field sets
- Optional parallel file input via a simple thread pool

## Requirements
- CMake >= 3.28
- Clang (tested with 17+) with C++23 module support
- Ninja (recommended)

## Build
```powershell
cmake --preset clang-ninja
cmake --build --preset clang-ninja-debug
```

Run tests (enable them first):
```powershell
cmake --preset clang-ninja -DRAIJSON_BUILD_TESTS=ON
cmake --build --preset clang-ninja-debug
ctest --preset clang-ninja-debug --output-on-failure -V
```

## Install and use with find_package
After configuring with presets or a manual build directory:
```powershell
cmake --build build/clang-ninja --target install
```

In a downstream project:
```cmake
find_package(RaiJson REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE RaiJson::RaiJson)
```

## Quick example
```cpp
import rai.json.json_field;
import rai.json.json_field_set;
import rai.json.json_io;

struct Point {
    int x{};
    int y{};
    const rai::json::IJsonFieldSet& jsonFields() const {
        static const auto fields = rai::json::makeJsonFieldSet<Point>(
            rai::json::makeJsonField(&Point::x, "x"),
            rai::json::makeJsonField(&Point::y, "y")
        );
        return fields;
    }
};

int main() {
    Point p{1, 2};
    std::string json = rai::json::getJsonContent(p); // {x:1,y:2}
    Point out{};
    rai::json::readJsonString(json, out);
}
```

## JsonEnumField

Enum型のメンバーを文字列として読み書きする場合に使用します。

```cpp
import rai.json.json_field;
import rai.json.json_field_set;
import rai.json.json_io;

enum class Color { Red, Green, Blue };

// Enum値と文字列の対応を定義
constexpr rai::json::EnumEntry<Color> colorEntries[] = {
    { Color::Red,   "red" },
    { Color::Green, "green" },
    { Color::Blue,  "blue" }
};

struct ColorHolder {
    Color color = Color::Red;

    const rai::json::IJsonFieldSet& jsonFields() const {
        static const auto colorMap = rai::json::makeJsonEnumMap(colorEntries);
        static const auto fields = rai::json::makeJsonFieldSet<ColorHolder>(
            rai::json::makeJsonEnumField(&ColorHolder::color, "color", colorMap)
        );
        return fields;
    }
};

int main() {
    ColorHolder holder;
    holder.color = Color::Green;
    std::string json = rai::json::getJsonContent(holder); // {color:"green"}

    ColorHolder restored{};
    rai::json::readJsonString(json, restored);
    // restored.color == Color::Green
}
```

## JsonPolymorphicField / JsonPolymorphicArrayField

ポリモーフィックなオブジェクト（基底クラスへのポインタ）を読み書きする場合に使用します。
派生型はファクトリ関数のマップで登録し、JSONにはタイプ識別キー（デフォルトは`"type"`）が含まれます。

```cpp
import rai.json.json_field;
import rai.json.json_field_set;
import rai.json.json_io;
import rai.collection.sorted_hash_array_map;
#include <memory>

// 基底クラス（jsonFields()を仮想関数として提供）
struct Shape {
    virtual ~Shape() = default;
    virtual const rai::json::IJsonFieldSet& jsonFields() const = 0;
};

// 派生クラス1
struct Circle : public Shape {
    double radius = 0.0;

    const rai::json::IJsonFieldSet& jsonFields() const override {
        static const auto fields = rai::json::makeJsonFieldSet<Circle>(
            rai::json::makeJsonField(&Circle::radius, "radius")
        );
        return fields;
    }
};

// 派生クラス2
struct Rectangle : public Shape {
    double width = 0.0;
    double height = 0.0;

    const rai::json::IJsonFieldSet& jsonFields() const override {
        static const auto fields = rai::json::makeJsonFieldSet<Rectangle>(
            rai::json::makeJsonField(&Rectangle::width, "width"),
            rai::json::makeJsonField(&Rectangle::height, "height")
        );
        return fields;
    }
};

// 型名からファクトリ関数へのマップを作成
using MapEntry = std::pair<std::string_view, std::function<std::unique_ptr<Shape>()>>;

inline const auto shapeEntriesMap = rai::collection::makeSortedHashArrayMap(
    MapEntry{ "Circle",    []() { return std::make_unique<Circle>(); } },
    MapEntry{ "Rectangle", []() { return std::make_unique<Rectangle>(); } }
);

struct Drawing {
    std::unique_ptr<Shape> mainShape;              // 単一オブジェクト
    std::vector<std::unique_ptr<Shape>> shapes;    // 配列

    const rai::json::IJsonFieldSet& jsonFields() const {
        static const auto fields = rai::json::makeJsonFieldSet<Drawing>(
            // 第4引数でタイプ識別キーをカスタマイズ可能（デフォルトは"type"）
            rai::json::JsonPolymorphicField(&Drawing::mainShape, "mainShape", shapeEntriesMap, "kind"),
            rai::json::JsonPolymorphicArrayField(&Drawing::shapes, "shapes", shapeEntriesMap, "kind")
        );
        return fields;
    }
};

int main() {
    Drawing drawing;
    drawing.mainShape = std::make_unique<Circle>();
    static_cast<Circle*>(drawing.mainShape.get())->radius = 5.0;

    auto rect = std::make_unique<Rectangle>();
    rect->width = 10.0;
    rect->height = 20.0;
    drawing.shapes.push_back(std::move(rect));

    std::string json = rai::json::getJsonContent(drawing);
    // {mainShape:{kind:"Circle",radius:5},shapes:[{kind:"Rectangle",width:10,height:20}]}

    Drawing restored{};
    rai::json::readJsonString(json, restored);
}
```

## writeJson / readJson を持つ型

`jsonFields()`を使わず、完全にカスタムのJSON入出力を行いたい場合は、
`writeJson(JsonWriter&) const`と`readJson(JsonParser&)`メソッドを実装します。

```cpp
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_io;

struct CustomData {
    int value = 0;
    std::string name;

    /// @brief JSONへの書き出し。
    void writeJson(rai::json::JsonWriter& writer) const {
        writer.startObject();
        writer.key("value");
        writer.writeObject(value);
        writer.key("name");
        writer.writeObject(name);
        writer.endObject();
    }

    /// @brief JSONからの読み込み。
    void readJson(rai::json::JsonParser& parser) {
        parser.startObject();
        while (!parser.nextIsEndObject()) {
            auto key = parser.nextKey();
            if (key == "value") {
                parser.readTo(value);
            } else if (key == "name") {
                parser.readTo(name);
            } else {
                parser.skipValue();  // 未知のキーはスキップ
            }
        }
        parser.endObject();
    }
};

int main() {
    CustomData data;
    data.value = 42;
    data.name = "test";

    // getJsonContent / readJsonString で通常通り使用可能
    std::string json = rai::json::getJsonContent(data);  // {value:42,name:"test"}

    CustomData restored;
    rai::json::readJsonString(json, restored);
    // restored.value == 42, restored.name == "test"
}
```

この方式は、`jsonFields()`ベースのフィールドセット内で他の型として使用することも可能です。
例えば、`JsonField`が参照するメンバー変数の型が`writeJson`/`readJson`を持っていれば、
自動的にそのメソッドが呼び出されます。

## Source overview
- `src/Common/SortedHashArrayMap.cppm`: Fixed-size hash + sorted array map for fast key lookup without allocations.
- `src/Common/ThreadPool.cppm`: Lightweight task queue for parallel I/O helpers.
- `src/IO/JsonTokenizer.cppm`: JSON5 tokenizer with comment/whitespace handling.
- `src/IO/JsonTokenManager.cppm`: Thread-safe token queue abstraction.
- `src/IO/JsonParser.cppm`: Token-based parser that builds values with strict type checks and unknown-key tracking.
- `src/IO/JsonWriter.cppm`: JSON5 writer with identifier-aware key emission and escaping.
- `src/IO/JsonField.cppm`: Field descriptors including enum and polymorphic support.
- `src/IO/JsonFieldSet.cppm`: Field-set reflection, (de)serialization glue, and polymorphic dispatch.
- `src/IO/JsonIO.cppm`: High-level read/write helpers for strings, files, and streams.

## License
Apache License 2.0. See [LICENSE](LICENSE).
