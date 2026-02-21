# RaiSerialization

**RaiSerialization** is a small, header-equivalent C++23 JSON5 toolkit implemented using C++ modules. It focuses on
fast, dependency-free parsing and convenient, declarative mappings between C++ types and JSON.

## Key features ✅
- Zero-dependency JSON5 tokenizer, parser, and writer
- Declarative field descriptors: `getRequiredField`, `getDefaultOmittedField`, `getInitialOmittedField`
- Enum and polymorphic converters: `getEnumConverter`, `getPolymorphicConverter`, `getPolymorphicArrayConverter`
- Polymorphic object support (single object and arrays) using type tags
- Small, fixed-capacity sorted-hash array map for fast key lookup without heap allocations
- Sequential/parallel JSON file loading with auto selection by file size

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
cmake --build --preset clang-ninja-debug --target RaiSerialization_JsonTest
```

## Tests 🧪
Tests are built when `RAIJSON_BUILD_TESTS` is enabled (the configure presets enable this by default).
Run the test suite with:

```powershell
ctest --preset clang-debug
```

Recommended workflow:

```powershell
cmake --preset clang
cmake --build --preset clang-debug --target RaiSerialization_JsonTest
ctest --preset clang-debug
```

Test logs are generated under `build/clang/Testing/Temporary/`.
Avoid running bare `ctest` from arbitrary directories; use the preset above to keep test outputs under `build/`.

## Install and use with find_package 📦
To install the library from a configured build directory:

```powershell
cmake --build build/clang-ninja --target install
```

In a downstream CMake project:

```cmake
find_package(RaiSerialization REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE RaiSerialization::RaiSerialization)
```

### Using the test helper (`JsonTestHelper.cppm`) 🧩
If you enable the test helper at install time, the package will export a helper target you can link against from downstream projects:

- Enable on install: configure with `-DRAISERIALIZATION_BUILD_TEST_HELPER=ON` (or set the option in your install step).
- After `make install` (or `cmake --build --target install`), use in your project:

```cmake
find_package(RaiSerialization REQUIRED)
add_executable(my_tests tests.cpp)
# Link the helper test module (provides test utilities implemented in JsonTestHelper.cppm)
target_link_libraries(my_tests PRIVATE RaiSerialization::RaiSerializationTest GTest::gtest)
```

Note: `RaiSerialization::RaiSerializationTest` is only available when the test helper was installed. In that case, the generated package config will also add a `find_dependency(GTest)` entry so GTest is discovered automatically.


## Quick example 📄
A minimal example showing field-based reflection:

```cpp
import rai.serialization.field_serializer;
import rai.serialization.object_serializer;
import rai.serialization.json_io;

struct Point {
    int x{};
    int y{};
    const rai::serialization::ObjectSerializer& serializer() const {
        static const auto fields = rai::serialization::getFieldSet(
            rai::serialization::getRequiredField(&Point::x, "x"),
            rai::serialization::getRequiredField(&Point::y, "y")
        );
        return fields;
    }
};

int main() {
    Point p{1, 2};
    std::string json = rai::serialization::getJsonContent(p); // {x:1,y:2}

    Point out{};
    rai::serialization::readJsonString(json, out);
}
```

## File input variants and unknown keys 🗂️
File loading supports sequential, parallel, and auto-selected paths. You can also collect unknown keys.

```cpp
import rai.serialization.field_serializer;
import rai.serialization.object_serializer;
import rai.serialization.json_io;

struct Config {
    int value = 0;
    const rai::serialization::ObjectSerializer& serializer() const {
        static const auto fields = rai::serialization::getFieldSet(
            rai::serialization::getRequiredField(&Config::value, "value")
        );
        return fields;
    }
};

int main() {
    Config cfg{};
    std::vector<std::string> unknownKeys;

    // Auto-select (small files -> sequential, large -> parallel)
    rai::serialization::readJsonFile("config.json", cfg, unknownKeys);

    // Explicit modes
    rai::serialization::readJsonFileSequential("config.json", cfg);
    rai::serialization::readJsonFileParallel("config.json", cfg);
}
```

## Enum converter example (getEnumConverter) 🔁
Serialize enum members as strings by defining `EnumEntry` values and using `getRequiredField` with an enum converter.
`getEnumConverter` accepts C arrays, `std::array`, or `std::span` of `EnumEntry`.

```cpp
import rai.serialization.field_serializer;
import rai.serialization.object_serializer;
import rai.serialization.json_io;

enum class Color { Red, Green, Blue };

struct ColorHolder {
    Color color = Color::Red;

    const rai::serialization::ObjectSerializer& serializer() const {
        static const auto colorConverter = rai::serialization::getEnumConverter({
            { Color::Red,   "red" },
            { Color::Green, "green" },
            { Color::Blue,  "blue" }
        });
        static const auto fields = rai::serialization::getFieldSet(
            rai::serialization::getRequiredField(&ColorHolder::color, "color", colorConverter)
        );
        return fields;
    }
};
```

## Polymorphic fields 🧩
Register derived-type factory functions in a map and the serializer will include a type key (default: `"type"`) in the JSON.
The type key can be customized when creating the polymorphic field.

```cpp
import rai.serialization.field_serializer;
import rai.serialization.object_serializer;
import rai.serialization.json_io;
import rai.collection.sorted_hash_array_map;
#include <memory>

struct Shape {
    virtual ~Shape() = default;
    virtual const rai::serialization::ObjectSerializer& serializer() const = 0;
};

struct Circle : public Shape {
    double radius = 0.0;
    const rai::serialization::ObjectSerializer& serializer() const override {
        static const auto fields = rai::serialization::getFieldSet(
            rai::serialization::getRequiredField(&Circle::radius, "radius")
        );
        return fields;
    }
};

struct Rectangle : public Shape {
    double width = 0.0;
    double height = 0.0;
    const rai::serialization::ObjectSerializer& serializer() const override {
        static const auto fields = rai::serialization::getFieldSet(
            rai::serialization::getRequiredField(&Rectangle::width, "width"),
            rai::serialization::getRequiredField(&Rectangle::height, "height")
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

    const rai::serialization::ObjectSerializer& serializer() const {
        static const auto mainShapeConverter =
            rai::serialization::getPolymorphicConverter<std::unique_ptr<Shape>>(
                shapeEntriesMap, "kind");
        static const auto shapesConverter =
            rai::serialization::getPolymorphicArrayConverter<decltype(shapes)>(
                shapeEntriesMap, "kind");
        static const auto fields = rai::serialization::getFieldSet(
            rai::serialization::getRequiredField(&Drawing::mainShape, "mainShape", mainShapeConverter),
            rai::serialization::getRequiredField(&Drawing::shapes, "shapes", shapesConverter)
        );
        return fields;
    }
};

> Note: Polymorphic converters accept an optional `allowNull` flag (default: `true`).
```

## Custom read/write methods (writeFormat / readFormat) ✍️
If you prefer full control, implement `void writeFormat(FormatWriter&) const` and
`void readFormat(FormatReader&)` on your type. These methods are also used automatically
when such types are encountered inside `FieldSerializer`-driven structures.

```cpp
import rai.serialization.format_io;
import rai.serialization.json_io;

struct CustomData {
    int value = 0;
    std::string name;

    void writeFormat(rai::serialization::FormatWriter& writer) const {
        writer.startObject();
        writer.key("value"); writer.writeObject(value);
        writer.key("name");  writer.writeObject(name);
        writer.endObject();
    }

    void readFormat(rai::serialization::FormatReader& parser) {
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
- `src/Serialization/Json/JsonTokenizer.cppm`: JSON5 tokenizer with comment and whitespace handling.
- `src/Serialization/TokenManager.cppm`: Token queue abstraction for thread-safe parsing.
- `src/Serialization/FormatIO.cppm`: Default format aliases (`FormatReader`/`FormatWriter`) used by serializer internals.
- `src/Serialization/Parser.cppm`: Token-based parser with strong type checks and unknown-key tracking.
- `src/Serialization/Json/JsonWriter.cppm`: JSON5 writer with identifier-aware key emission and escaping.
- `src/Serialization/ObjectConverter.cppm`: Converters for primitives, enums, containers, pointers, and custom types.
- `src/Serialization/PolymorphicConverter.cppm`: Polymorphic converters with type tags.
- `src/Serialization/FieldSerializer.cppm`: Field descriptors and omit behaviors.
- `src/Serialization/ObjectSerializer.cppm`: Field-set reflection and (de)serialization glue.
- `src/Serialization/Json/JsonIO.cppm`: High-level helpers for reading/writing strings, files, and streams.

---

## License 📜
Apache License 2.0. See [LICENSE](LICENSE).

