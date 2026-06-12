# RaiSerialization

**RaiSerialization** is a small, header-equivalent C++23 JSON5 toolkit implemented using C++ modules. It focuses on
fast, dependency-free parsing and convenient, declarative mappings between C++ types and JSON.

## Key features ✅
- Zero-dependency JSON5 tokenizer, JsonParser, and writer
- Declarative field descriptors: `getRequiredField`, `getDefaultOmittedField`, `getInitialOmittedField`, `getInitialAlwaysField`
- Property-based field descriptors: `getProperty`, `getRequiredProperty`, `getDefaultOmittedProperty`, `getInitialOmittedProperty`, `getInitialAlwaysProperty`
- Enum and polymorphic converters: `getEnumConverter`, `getPolymorphicConverter`, `getPolymorphicArrayConverter`
- Polymorphic object support (single object and arrays) using type tags
- Small, fixed-capacity sorted-hash array map for fast key lookup without heap allocations
- Sequential/parallel JSON file loading with auto selection by file size

## Requirements ⚙️
- CMake >= 3.28
- Clang/clang++ (tested with Clang 17+) with C++23 modules support, or `clang-cl` for MSVC-compatible builds
- Ninja (recommended)

> Note: The CMake configure presets in this repository set `RAISERIALIZATION_BUILD_TESTS=ON` by default.

## Build (using presets) 🔧
Configure and build using the provided presets (recommended):

```powershell
cmake --preset clang
cmake --build --preset clang-debug
```

Build a specific test target (example):

```powershell
cmake --build --preset clang-debug --target RaiSerialization_JsonTest
```

## Tests 🧪
Tests are built when `RAISERIALIZATION_BUILD_TESTS` is enabled (the configure presets enable this by default).
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
cmake --build build/clang --target install
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
import rai.serialization.core;
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

## Property-based field access 🧩
When a type exposes values through getter/setter methods instead of public member variables, use `getProperty` or `getRequiredProperty`.

```cpp
import rai.serialization.core;
import rai.serialization::json_io;

struct Foo {
    std::string bar;

    const std::string& getBar() const {
        return bar;
    }

    void setBar(std::string_view value) {
        bar = value;
    }

    const rai::serialization::ObjectSerializer& serializer() const {
        static const auto fields = rai::serialization::getFieldSet(
            rai::serialization::getRequiredProperty(
                &Foo::getBar, &Foo::setBar, "bar")
        );
        return fields;
    }
};
```

## Using getObjectSerializerConverter 🧩
Use `getObjectSerializerConverter<T>(serializer)` when you want to serialize a type
through an explicit `ObjectSerializer` instance rather than relying on the default
field-based helper converter.

```cpp
import rai.serialization.core;
import rai.serialization.json_io;

struct Item {
    int x{};
    std::string y{};
};

static const auto itemFields = rai::serialization::getFieldSet(
    rai::serialization::getRequiredField(&Item::x, "x"),
    rai::serialization::getRequiredField(&Item::y, "y")
);

int main() {
    Item original{7, "converter"};
    auto converter = rai::serialization::getObjectSerializerConverter<Item>(itemFields);
    std::string json = rai::serialization::getJsonContent(original, converter);
    Item parsed{};
    rai::serialization::readJsonString(json, parsed, converter);
}
```

Notes:
- `getObjectSerializerConverter<T>(serializer)` is the preferred way to override nested object fields explicitly.
- Explicit converters satisfy `IsObjectConverter` by defining `using Value = T`,
  `void write(FormatWriter&, const T&) const`, and `void read(FormatReader&, T&) const`.
  `read` writes into the supplied object instead of returning a value, so converters can
  deserialize non-copyable and non-movable types.

## Using getMapConverter 🧩
Use `getMapConverter<Map>(keyConverter, valueConverter)` when you want to
serialize a map-like value and explicitly choose how keys and values are
converted. The JSON shape is an array of key/value pairs: `[[key, value], ...]`.

```cpp
import rai.serialization.core;
import rai.serialization.json_io;
#include <map>

struct Item {
    int id{};
    std::string name{};
};

struct ItemKey {
    std::string group{};
    int index{};

    bool operator<(const ItemKey& other) const {
        return group < other.group ||
            (group == other.group && index < other.index);
    }
};

static const auto keyFields = rai::serialization::getFieldSet(
    rai::serialization::getRequiredField(&ItemKey::group, "group"),
    rai::serialization::getRequiredField(&ItemKey::index, "index")
);

static const auto itemFields = rai::serialization::getFieldSet(
    rai::serialization::getRequiredField(&Item::id, "id"),
    rai::serialization::getRequiredField(&Item::name, "name")
);

int main() {
    using ItemMap = std::map<ItemKey, Item>;

    auto keyConverter =
        rai::serialization::getObjectSerializerConverter<ItemKey>(keyFields);
    auto itemConverter =
        rai::serialization::getObjectSerializerConverter<Item>(itemFields);
    auto mapConverter =
        rai::serialization::getMapConverter<ItemMap>(keyConverter, itemConverter);

    ItemMap original{
        {{"a", 1}, {10, "one"}},
        {{"b", 2}, {20, "two"}}
    };

    std::string json = rai::serialization::getJsonContent(original, mapConverter);
    // json == [[{group:"a",index:1},{id:10,name:"one"}],
    //          [{group:"b",index:2},{id:20,name:"two"}]]

    ItemMap parsed;
    rai::serialization::readJsonString(json, parsed, mapConverter);
}
```

For keys and values that already have default converters, use
`getMapConverter<Map>()`. You can also provide only the mapped-value converter
with `getMapConverter<Map>(valueConverter)`, in which case the key uses its
default converter. For polymorphic values, pass a polymorphic converter
as the value converter:

```cpp
using ShapeMap = std::map<std::string, std::unique_ptr<Shape>>;

static const auto shapeConverter =
    rai::serialization::getPolymorphicConverter<std::unique_ptr<Shape>>(
        shapeEntriesMap, "kind");
static const auto shapeMapConverter =
    rai::serialization::getMapConverter<ShapeMap>(shapeConverter);
```

Notes:
- `getMapConverter` writes ordinary key/value entry arrays. Keep using `getColumnarMapConverter` when you need the compact columnar map format.

## Using getColumnarContainerConverter 🧩
Use `getColumnarContainerConverter<T>(serializer)` for row-oriented containers when you want JSON output in a compact, columnar table format.

```cpp
import rai.serialization.core;
import rai.serialization.json_io;

struct Item {
    int id{};
    std::string name{};
};

static const auto itemFields = rai::serialization::getFieldSet(
    rai::serialization::getRequiredField(&Item::id, "id"),
    rai::serialization::getRequiredField(&Item::name, "name")
);

static auto getColumnarItemConverter() {
    return rai::serialization::getColumnarContainerConverter<Item>(itemFields);
}

int main() {
    std::vector<Item> original{
        {1, "one"},
        {2, "two"}
    };

    auto converter = getColumnarItemConverter();
    std::string json = rai::serialization::getJsonContent(original, converter);
    // json == [["id","name"],[1,"one"],[2,"two"]]

    std::vector<Item> parsed;
    rai::serialization::readJsonString(json, parsed, converter);
}
```

Notes:
- `getColumnarContainerConverter<T>(serializer)` is useful for serializing tabular data as a header row plus value rows.

## File input variants and unknown keys 🗂️
File loading supports sequential, parallel, and auto-selected paths. You can also collect unknown keys.

```cpp
import rai.serialization.core;
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
import rai.serialization.core;
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
import rai.serialization.core;
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

### Polymorphic fields with external serializers
If the polymorphic classes do not expose `serializer()`, register each concrete type with
`PolymorphicSerializerEntry`. The entry stores the runtime type, factory, and external
`ObjectSerializer`.

```cpp
struct Shape {
    virtual ~Shape() = default;
};

struct Circle : Shape {
    double radius = 0.0;
};

struct Rectangle : Shape {
    double width = 0.0;
    double height = 0.0;
};

static const auto circleFields = rai::serialization::getFieldSet(
    rai::serialization::getRequiredField(&Circle::radius, "radius")
);

static const auto rectangleFields = rai::serialization::getFieldSet(
    rai::serialization::getRequiredField(&Rectangle::width, "width"),
    rai::serialization::getRequiredField(&Rectangle::height, "height")
);

using ShapePtr = std::unique_ptr<Shape>;
using ShapeEntry = rai::serialization::PolymorphicSerializerEntry<ShapePtr>;
using ShapeMapEntry = std::pair<std::string_view, ShapeEntry>;

inline const auto shapeEntriesMap = rai::collection::makeSortedHashArrayMap(
    ShapeMapEntry{ "Circle", ShapeEntry{
        std::type_index(typeid(Circle)),
        []() -> ShapePtr { return std::make_unique<Circle>(); },
        circleFields
    } },
    ShapeMapEntry{ "Rectangle", ShapeEntry{
        std::type_index(typeid(Rectangle)),
        []() -> ShapePtr { return std::make_unique<Rectangle>(); },
        rectangleFields
    } }
);
```

Writing uses `typeid(*ptr)` to select the matching entry, so the pointed-to base type must be polymorphic
(for example, declare a virtual destructor).

## Custom read/write methods (write / read) ✍️
If you prefer full control, implement `void write(FormatWriter&) const` and
`void read(FormatReader&)` on your type. These methods are also used automatically
when such types are encountered inside `FieldSerializer`-driven structures.

```cpp
import rai.serialization.core;
import rai.serialization.json_io;

struct CustomData {
    int value = 0;
    std::string name;

    void write(rai::serialization::FormatWriter& writer) const {
        writer.startObject();
        writer.key("value"); writer.writeObject(value);
        writer.key("name");  writer.writeObject(name);
        writer.endObject();
    }

    void read(rai::serialization::FormatReader& parser) {
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
- `src/Json/JsonTokenizer.cppm`: JSON5 tokenizer with comment and whitespace handling.
- `src/Core/TokenManager.cppm`: Token queue abstraction for thread-safe parsing.
- `src/Core/FormatIO.cppm`: Default format aliases (`FormatReader`/`FormatWriter`) used by serializer internals.
- `src/Json/JsonParser.cppm`: Token-based JsonParser with strong type checks and unknown-key tracking.
- `src/Json/JsonWriter.cppm`: JSON5 writer with identifier-aware key emission and escaping.
- `src/Core/ObjectConverter.cppm`: Converters for primitives, enums, containers, pointers, and custom types.
- `src/Core/PolymorphicConverter.cppm`: Polymorphic converters with type tags.
- `src/Core/FieldSerializer.cppm`: Field descriptors and omit behaviors.
- `src/Core/ObjectSerializer.cppm`: Field-set reflection and (de)serialization glue.
- `src/Json/JsonIO.cppm`: High-level helpers for reading/writing strings, files, and streams.

---

## License 📜
Apache License 2.0. See [LICENSE](LICENSE).

