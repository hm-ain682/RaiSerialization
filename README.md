# RaiJson

**RaiJson** is a small, header-equivalent C++23 JSON5 toolkit implemented using C++ modules. It focuses on
fast, dependency-free parsing and convenient, declarative mappings between C++ types and JSON.

## Key features ✅
- Zero-dependency JSON5 tokenizer, parser, and writer
- Declarative field descriptors: `JsonField`, `JsonEnumField`, `JsonPolymorphicField` for struct ↔ JSON conversion
- Polymorphic object support (single object and arrays) using type tags
- Small, fixed-capacity sorted-hash array map for fast key lookup without heap allocations
- Optional parallel input helpers via a lightweight thread pool

## Requirements ⚙️
- CMake >= 3.28
- Clang/clang++ (tested with Clang 17+) with C++23 modules support, or `clang-cl` for MSVC-compatible builds
- Ninja (recommended)

> Note: The CMake configure presets in this repository set `RAIJSON_BUILD_TESTS=ON` by default.

## Build (using presets) 🔧
Configure and build using the provided presets (recommended):

```powershell
cmake --preset clang-ninja
cmake --build --preset clang-ninja-debug
```

Build a specific test target (example):

```powershell
cmake --build --preset clang-ninja-debug --target RaiJson_JsonTest
```

## Tests 🧪
Tests are built when `RAIJSON_BUILD_TESTS` is enabled (the configure presets enable this by default).
Run the test suite with:

```powershell
ctest --preset clang-ninja-debug --output-on-failure -V
```

## Install and use with find_package 📦
To install the library from a configured build directory:

```powershell
cmake --build build/clang-ninja --target install
```

In a downstream CMake project:

```cmake
find_package(RaiJson REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE RaiJson::RaiJson)
```

## Quick example 📄
A minimal example showing `JsonField` based reflection:

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

## Enum fields example (JsonEnumField) 🔁
Serialize enum members as strings by defining an enum map and using `makeJsonEnumField`.

```cpp
import rai.json.json_field;
import rai.json.json_field_set;
import rai.json.json_io;

enum class Color { Red, Green, Blue };

struct ColorHolder {
    Color color = Color::Red;

    const rai::json::IJsonFieldSet& jsonFields() const {
        static const auto colorMap = rai::json::makeJsonEnumMap({
            { Color::Red,   "red" },
            { Color::Green, "green" },
            { Color::Blue,  "blue" }
        });
        static const auto fields = rai::json::makeJsonFieldSet<ColorHolder>(
            rai::json::makeJsonEnumField(&ColorHolder::color, "color", colorMap)
        );
        return fields;
    }
};
```

## Polymorphic fields (JsonPolymorphicField / JsonPolymorphicArrayField) 🧩
Register derived-type factory functions in a map and the serializer will include a type key (default: `"type"`) in the JSON.
The type key can be customized when creating the polymorphic field.

```cpp
import rai.json.json_field;
import rai.json.json_field_set;
import rai.json.json_io;
import rai.collection.sorted_hash_array_map;
#include <memory>

struct Shape {
    virtual ~Shape() = default;
    virtual const rai::json::IJsonFieldSet& jsonFields() const = 0;
};

struct Circle : public Shape {
    double radius = 0.0;
    const rai::json::IJsonFieldSet& jsonFields() const override {
        static const auto fields = rai::json::makeJsonFieldSet<Circle>(
            rai::json::makeJsonField(&Circle::radius, "radius")
        );
        return fields;
    }
};

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

using MapEntry = std::pair<std::string_view, std::function<std::unique_ptr<Shape>()>>;
inline const auto shapeEntriesMap = rai::collection::makeSortedHashArrayMap(
    MapEntry{ "Circle",    []() { return std::make_unique<Circle>(); } },
    MapEntry{ "Rectangle", []() { return std::make_unique<Rectangle>(); } }
);

struct Drawing {
    std::unique_ptr<Shape> mainShape;
    std::vector<std::unique_ptr<Shape>> shapes;

    const rai::json::IJsonFieldSet& jsonFields() const {
        static const auto fields = rai::json::makeJsonFieldSet<Drawing>(
            rai::json::makeJsonPolymorphicField(&Drawing::mainShape, "mainShape", shapeEntriesMap, "kind"),
            rai::json::makeJsonPolymorphicArrayField(&Drawing::shapes, "shapes", shapeEntriesMap, "kind")
        );
        return fields;
    }
};
```

## Custom read/write methods (writeJson / readJson) ✍️
If you prefer full control, implement `void writeJson(JsonWriter&) const` and `void readJson(JsonParser&)` on your type. These methods are also used automatically when such types are encountered inside `JsonField`-driven structures.

```cpp
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_io;

struct CustomData {
    int value = 0;
    std::string name;

    void writeJson(rai::json::JsonWriter& writer) const {
        writer.startObject();
        writer.key("value"); writer.writeObject(value);
        writer.key("name");  writer.writeObject(name);
        writer.endObject();
    }

    void readJson(rai::json::JsonParser& parser) {
        parser.startObject();
        while (!parser.nextIsEndObject()) {
            auto key = parser.nextKey();
            if (key == "value") {
                parser.readTo(value);
            } else if (key == "name") {
                parser.readTo(name);
            } else {
                parser.skipValue();
            }
        }
        parser.endObject();
    }
};
```

## Source overview 🔍
- `src/Common/SortedHashArrayMap.cppm`: Fixed-size hash + sorted array map for fast key lookup without allocations.
- `src/Common/ThreadPool.cppm`: Lightweight task queue used by parallel I/O helpers.
- `src/IO/JsonTokenizer.cppm`: JSON5 tokenizer with comment and whitespace handling.
- `src/IO/JsonTokenManager.cppm`: Token queue abstraction for thread-safe parsing.
- `src/IO/JsonParser.cppm`: Token-based parser with strong type checks and unknown-key tracking.
- `src/IO/JsonWriter.cppm`: JSON5 writer with identifier-aware key emission and escaping.
- `src/IO/JsonField.cppm`: Field descriptors, including enum and polymorphic support.
- `src/IO/JsonFieldSet.cppm`: Field-set reflection and (de)serialization glue.
- `src/IO/JsonIO.cppm`: High-level helpers for reading/writing strings, files, and streams.

---

## License 📜
Apache License 2.0. See [LICENSE](LICENSE).

