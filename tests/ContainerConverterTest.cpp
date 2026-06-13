// ContainerConverter/ColumnarContainerConverter/ColumnarMapConverter 専用テスト
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <functional>
#include <array>
import rai.serialization.core;
import rai.serialization.json;
import rai.serialization.json_io;
import rai.serialization.test_helper;
import rai.serialization.token_manager;
import rai.collection.sorted_hash_array_map;

using namespace rai::serialization;
using namespace rai::serialization::test;

// ********************************************************************************
// ContainerConverter テスト
// ********************************************************************************

/// @brief JsonContainerFieldのテスト用の単純なカスタム型。
struct Tag {
    std::string label;
    int priority = 0;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&Tag::label, "label"),
            getRequiredField(&Tag::priority, "priority")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool operator==(const Tag& other) const {
        return label == other.label && priority == other.priority;
    }
};

/// @brief JsonContainerFieldをvectorで使用するテスト用構造体。
struct SetFieldVectorHolder {
    std::vector<Tag> tags;

    const ObjectSerializer& serializer() const {
        static const auto containerConverter =
            getContainerConverter<decltype(tags)>();
        static const auto fields = getFieldSet(
            getRequiredField(&SetFieldVectorHolder::tags, "tags", containerConverter)
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const SetFieldVectorHolder& other) const {
        return tags == other.tags;
    }
};

/// @brief JsonContainerFieldのvectorでの読み書きテスト。
TEST(JsonContainerFieldTest, VectorReadWriteRoundTrip) {
    SetFieldVectorHolder original;
    original.tags = {{"first", 1}, {"second", 2}, {"third", 3}};
    testJsonRoundTrip(original,
        "{tags:[{label:\"first\",priority:1},{label:\"second\",priority:2},"
        "{label:\"third\",priority:3}]}");
}

/// @brief JsonContainerFieldの空vectorでの読み書きテスト。
TEST(JsonContainerFieldTest, VectorEmptyReadWriteRoundTrip) {
    SetFieldVectorHolder original;
    original.tags = {};
    testJsonRoundTrip(original, "{tags:[]}");
}

/// @brief JsonContainerFieldをstd::setで使用するテスト用構造体。
struct SetFieldSetHolder {
    std::set<std::string> tags;

    const ObjectSerializer& serializer() const {
        static const auto containerConverter =
            getContainerConverter<decltype(tags)>();
        static const auto fields = getFieldSet(
            getRequiredField(&SetFieldSetHolder::tags, "tags", containerConverter)
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const SetFieldSetHolder& other) const {
        return tags == other.tags;
    }
};

/// @brief JsonContainerFieldのstd::setでの読み書きテスト。
TEST(JsonContainerFieldTest, SetReadWriteRoundTrip) {
    SetFieldSetHolder original;
    original.tags = {"alpha", "beta", "gamma"};
    // std::setはソートされるため、出力順序もソート済み
    testJsonRoundTrip(original, "{tags:[\"alpha\",\"beta\",\"gamma\"]}");
}

/// @brief JsonContainerFieldの空std::setでの読み書きテスト。
TEST(JsonContainerFieldTest, SetEmptyReadWriteRoundTrip) {
    SetFieldSetHolder original;
    original.tags = {};
    testJsonRoundTrip(original, "{tags:[]}");
}

/// @brief JsonContainerFieldを複雑な要素型（オブジェクト）で使用するテスト用構造体。
struct Point {
    int x = 0;
    int y = 0;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&Point::x, "x"),
            getRequiredField(&Point::y, "y")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

/// @brief JsonContainerFieldを複雑な要素型で使用するテスト用構造体。
struct SetFieldObjectHolder {
    std::vector<Point> points;

    const ObjectSerializer& serializer() const {
        static const auto containerConverter =
            getContainerConverter<decltype(points)>();
        static const auto fields = getFieldSet(
            getRequiredField(&SetFieldObjectHolder::points, "points", containerConverter)
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const SetFieldObjectHolder& other) const {
        return points == other.points;
    }
};

/// @brief JsonContainerFieldの複雑な要素型での読み書きテスト。
TEST(JsonContainerFieldTest, ObjectElementReadWriteRoundTrip) {
    SetFieldObjectHolder original;
    original.points = {{1, 2}, {3, 4}, {5, 6}};
    testJsonRoundTrip(original, "{points:[{x:1,y:2},{x:3,y:4},{x:5,y:6}]}");
}

// ********************************************************************************
// Tests: ensure element-converter selection is used for container/variant/unique_ptr
// ********************************************************************************

struct RWElement {
    int x = 0;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&RWElement::x, "x")
        );
        return fields;
    }

    bool operator==(const RWElement& other) const {
        return x == other.x;
    }
};

TEST(JsonElementConverterTest, ContainerUsesElementConverter) {
    struct Holder {
        std::vector<RWElement> v;
        const ObjectSerializer& serializer() const {
            static const auto containerConverter =
                getContainerConverter<decltype(v)>();
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::v, "v", containerConverter)
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            return v == other.v;
        }
        bool equals(const Holder& other) const {
            return *this == other;
        }
    };

    Holder original;
    RWElement e;
    e.x = 11;
    original.v.push_back(e);
    testJsonRoundTrip(original, "{v:[{x:11}]}");
}

TEST(JsonElementConverterTest, NestedContainerUsesElementConverter) {
    struct Holder {
        std::vector<std::vector<RWElement>> v;
        const ObjectSerializer& serializer() const {
            static const SerializerConverter<RWElement> innerElemConv{};
            using RWElementVector = std::vector<RWElement>;
            static const auto innerConverter =
                getContainerConverter<RWElementVector>(innerElemConv);
            static const auto containerConverter =
                getContainerConverter<decltype(v)>(innerConverter);
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::v, "v", containerConverter)
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            return v == other.v;
        }
        bool equals(const Holder& other) const {
            return *this == other;
        }
    };

    Holder original;
    original.v.push_back({RWElement{1}, RWElement{2}});
    testJsonRoundTrip(original, "{v:[[{x:1},{x:2}]]}" );
}

TEST(JsonElementConverterExplicitTest, ContainerOfEnumWithExplicitContainerConverter) {
    enum class Color { Red, Blue };
    constexpr EnumEntry<Color> entries[] = {{Color::Red, "Red"}, {Color::Blue, "Blue"}};
    static const auto enumConverter = getEnumConverter(entries);

    struct Holder {
        std::vector<Color> v;
        const ObjectSerializer& serializer() const {
            static const auto containerConverter =
                getContainerConverter<decltype(v)>(enumConverter);
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::v, "v", containerConverter)
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            return v == other.v;
        }
        bool equals(const Holder& other) const {
            return *this == other;
        }
    };

    Holder original;
    original.v = {Color::Red, Color::Blue};
    testJsonRoundTrip(original, "{v:[\"Red\",\"Blue\"]}");
}

TEST(JsonElementConverterExplicitTest, ContainerWithExplicitElementConverter) {
    struct Holder {
        std::vector<RWElement> v;
        const ObjectSerializer& serializer() const {
            static const SerializerConverter<RWElement> elementConverter{};
            static const auto containerConverter =
                getContainerConverter<decltype(v)>(elementConverter);
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::v, "v", containerConverter)
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            return v == other.v;
        }
        bool equals(const Holder& other) const {
            return *this == other;
        }
    };

    Holder original;
    RWElement e;
    e.x = 11;
    original.v.push_back(e);
    testJsonRoundTrip(original, "{v:[{x:11}]}");
}

// ********************************************************************************
// テストカテゴリ：ポインタとポインタのvector
// ********************************************************************************

/// @brief ポインタを含む構造体。
struct PointerHolder {
    std::unique_ptr<int> ptr;
    std::vector<std::unique_ptr<std::string>> ptrVec;

    const ObjectSerializer& serializer() const {
            // Provide explicit element/container converter for vector of unique_ptr<string>
            using Element = std::unique_ptr<std::string>;
            auto& elementConverter = getPointerConverter<Element>();
            static const auto uniquePtrConverter =
                getPointerConverter<decltype(ptr)>();
            static const auto containerConverter =
                getContainerConverter<decltype(ptrVec)>(elementConverter);
            static const auto fields = getFieldSet(
                getRequiredField(&PointerHolder::ptr, "ptr", uniquePtrConverter),
                getRequiredField(&PointerHolder::ptrVec, "ptrVec", containerConverter)
            );
            return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const PointerHolder& other) const {
        bool ptrMatch = (ptr == nullptr && other.ptr == nullptr) ||
                        (ptr != nullptr && other.ptr != nullptr && *ptr == *other.ptr);
        if (!ptrMatch) {
            return false;
        }

        if (ptrVec.size() != other.ptrVec.size()) {
            return false;
        }
        for (size_t i = 0; i < ptrVec.size(); ++i) {
            bool elemMatch = (ptrVec[i] == nullptr && other.ptrVec[i] == nullptr) ||
                             (ptrVec[i] != nullptr && other.ptrVec[i] != nullptr &&
                              *ptrVec[i] == *other.ptrVec[i]);
            if (!elemMatch) {
                return false;
            }
        }
        return true;
    }
};

/// @brief ポインタとvectorの読み書きテスト。
TEST(JsonPointerTest, ReadWriteRoundTrip) {
    // テスト用に異なる値を設定
    PointerHolder original;
    original.ptr = std::make_unique<int>(999);
    original.ptrVec.push_back(std::make_unique<std::string>("first"));
    original.ptrVec.push_back(nullptr);
    original.ptrVec.push_back(std::make_unique<std::string>("third"));

    testJsonRoundTrip(original, "{ptr:999,ptrVec:[\"first\",null,\"third\"]}");
}

// ********************************************************************************
// ColumnarContainerConverter テスト
// ********************************************************************************

struct ColumnarJsonTestItem {
    int id = 0;
    std::string name;

    bool operator==(const ColumnarJsonTestItem& other) const {
        return id == other.id && name == other.name;
    }
};

static auto getColumnarContainerConverterTest() {
    static const auto fields = getFieldSet(
        getRequiredField(&ColumnarJsonTestItem::id, "id"),
        getRequiredField(&ColumnarJsonTestItem::name, "name")
    );
    return getColumnarContainerConverter<ColumnarJsonTestItem>(fields);
}

TEST(JsonIOConverterTest, ColumnarContainerConverterRoundTrip) {
    std::vector<ColumnarJsonTestItem> original{
        {1, "one"},
        {2, "two"}
    };
    auto converter = getColumnarContainerConverterTest();

    testJsonRoundTrip(original,
        "[[\"id\",\"name\"],[1,\"one\"],[2,\"two\"]]",
        converter);
}

// ********************************************************************************
// MapConverter テスト
// ********************************************************************************

TEST(JsonIOConverterTest, MapConverterScalarRoundTrip) {
    std::map<std::string, int> original{
        {"a", 1},
        {"b", 2}
    };
    const auto& converter = getMapConverter<decltype(original)>();

    testJsonRoundTrip(original, "[[\"a\",1],[\"b\",2]]", converter);
}

struct MapConverterObjectKey {
    std::string group;
    int index = 0;

    bool operator<(const MapConverterObjectKey& other) const {
        return group < other.group ||
            (group == other.group && index < other.index);
    }

    bool operator==(const MapConverterObjectKey& other) const {
        return group == other.group && index == other.index;
    }
};

struct MapConverterObjectValue {
    int id = 0;
    std::string name;

    bool operator==(const MapConverterObjectValue& other) const {
        return id == other.id && name == other.name;
    }
};

static auto getMapConverterObjectKeyConverter() {
    static const auto fields = getFieldSet(
        getRequiredField(&MapConverterObjectKey::group, "group"),
        getRequiredField(&MapConverterObjectKey::index, "index")
    );
    return getObjectSerializerConverter<MapConverterObjectKey>(fields);
}

static auto getMapConverterObjectValueConverter() {
    static const auto fields = getFieldSet(
        getRequiredField(&MapConverterObjectValue::id, "id"),
        getRequiredField(&MapConverterObjectValue::name, "name")
    );
    return getObjectSerializerConverter<MapConverterObjectValue>(fields);
}

TEST(JsonIOConverterTest, MapConverterObjectSerializerRoundTrip) {
    std::map<MapConverterObjectKey, MapConverterObjectValue> original{
        {{"a", 1}, {10, "one"}},
        {{"b", 2}, {20, "two"}}
    };
    auto keyConverter = getMapConverterObjectKeyConverter();
    auto valueConverter = getMapConverterObjectValueConverter();
    auto converter = getMapConverter<decltype(original)>(keyConverter, valueConverter);

    testJsonRoundTrip(original,
        "[[{group:\"a\",index:1},{id:10,name:\"one\"}],"
        "[{group:\"b\",index:2},{id:20,name:\"two\"}]]",
        converter);
}

struct MapPolymorphicBase {
    virtual ~MapPolymorphicBase() = default;
    virtual const ObjectSerializer& serializer() const {
        static const auto fields = FieldsObjectSerializer<MapPolymorphicBase>{};
        return fields;
    }
    virtual bool equals(const MapPolymorphicBase& other) const = 0;
};

struct MapPolymorphicOne : MapPolymorphicBase {
    int x = 0;

    const ObjectSerializer& serializer() const override {
        static const auto fields = getFieldSet(
            getRequiredField(&MapPolymorphicOne::x, "x")
        );
        return fields;
    }

    bool equals(const MapPolymorphicBase& other) const override {
        const auto* typed = dynamic_cast<const MapPolymorphicOne*>(&other);
        return typed != nullptr && x == typed->x;
    }
};

struct MapPolymorphicTwo : MapPolymorphicBase {
    std::string s;

    const ObjectSerializer& serializer() const override {
        static const auto fields = getFieldSet(
            getRequiredField(&MapPolymorphicTwo::s, "s")
        );
        return fields;
    }

    bool equals(const MapPolymorphicBase& other) const override {
        const auto* typed = dynamic_cast<const MapPolymorphicTwo*>(&other);
        return typed != nullptr && s == typed->s;
    }
};

using MapPolymorphicPtr = std::unique_ptr<MapPolymorphicBase>;
inline const auto mapPolymorphicEntries = std::array{
    makePolymorphicTypeEntry<MapPolymorphicOne>("One",
        [](JsonParser&) -> MapPolymorphicPtr {
            return std::make_unique<MapPolymorphicOne>();
        }),
    makePolymorphicTypeEntry<MapPolymorphicTwo>("Two",
        [](JsonParser&) -> MapPolymorphicPtr {
            return std::make_unique<MapPolymorphicTwo>();
        })
};

struct MapPolymorphicHolder {
    std::map<std::string, MapPolymorphicPtr> items;

    const ObjectSerializer& serializer() const {
        static const auto valueConverter =
            getPolymorphicConverter<MapPolymorphicPtr>(
                mapPolymorphicEntries, "kind");
        static const auto mapConverter =
            getMapConverter<decltype(items)>(valueConverter);
        static const auto fields = getFieldSet(
            getRequiredField(&MapPolymorphicHolder::items, "items", mapConverter)
        );
        return fields;
    }

    bool equals(const MapPolymorphicHolder& other) const {
        if (items.size() != other.items.size()) {
            return false;
        }
        for (const auto& [key, value] : items) {
            auto it = other.items.find(key);
            if (it == other.items.end()) {
                return false;
            }
            if (value == nullptr || it->second == nullptr) {
                if (value != nullptr || it->second != nullptr) {
                    return false;
                }
                continue;
            }
            if (!value->equals(*it->second)) {
                return false;
            }
        }
        return true;
    }
};

TEST(JsonIOConverterTest, MapConverterPolymorphicUniquePtrRoundTrip) {
    MapPolymorphicHolder original;
    original.items.emplace("none", nullptr);

    auto one = std::make_unique<MapPolymorphicOne>();
    one->x = 10;
    original.items.emplace("one", std::move(one));

    auto two = std::make_unique<MapPolymorphicTwo>();
    two->s = "abc";
    original.items.emplace("two", std::move(two));

    testJsonRoundTrip(original,
        "{items:[[\"none\",null],[\"one\",{kind:\"One\",x:10}],"
        "[\"two\",{kind:\"Two\",s:\"abc\"}]]}");
}

// ********************************************************************************
// ColumnarMapConverter テスト
// ********************************************************************************

struct MapKey {
    std::string A;
    int B = 0;

    bool operator<(const MapKey& other) const {
        return A < other.A || (A == other.A && B < other.B);
    }

    bool operator==(const MapKey& other) const {
        return A == other.A && B == other.B;
    }
};

struct MapValue {
    bool C = false;
    std::string D;

    bool operator==(const MapValue& other) const {
        return C == other.C && D == other.D;
    }
};

static auto getColumnarMapConverterTest() {
    static const auto keyFields = getFieldSet(
        getRequiredField(&MapKey::A, "A"),
        getRequiredField(&MapKey::B, "B")
    );
    static const auto valueFields = getFieldSet(
        getRequiredField(&MapValue::D, "D"),
        getRequiredField(&MapValue::C, "C")
    );
    return getColumnarMapConverter<std::map<MapKey, MapValue>>(keyFields, valueFields);
}

TEST(JsonIOConverterTest, ColumnarMapConverterObjectRoundTrip) {
    std::map<MapKey, MapValue> original{
        {{"a1", 1}, {true, "d1"}},
        {{"a2", 2}, {false, "d2"}}
    };
    auto converter = getColumnarMapConverterTest();

    testJsonRoundTrip(original,
        "["
            "[[\"A\",\"B\"],[\"D\",\"C\"]],"
            "[[\"a1\",1],[\"d1\",true]],"
            "[[\"a2\",2],[\"d2\",false]]"
        "]",
        converter);
}

TEST(JsonIOConverterTest, ColumnarMapConverterScalarRoundTrip) {
    std::map<std::string, int> original{
        {"k1", 1},
        {"k2", 2}
    };
    auto converter = getColumnarMapConverter<decltype(original)>();

    testJsonRoundTrip(original,
        "[[\"Key\",\"Value\"],[\"k1\",1],[\"k2\",2]]",
        converter);
}
