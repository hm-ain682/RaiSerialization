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
            rai::json::JsonField(&Point::x, "x"),
            rai::json::JsonField(&Point::y, "y")
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
