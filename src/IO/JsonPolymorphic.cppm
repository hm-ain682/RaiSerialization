module;
#include <memory>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>
#include <bitset>
#include <functional>
#include <ranges>
#include <typeinfo>
#include <vector>
#include <set>
#include <unordered_set>

export module rai.json.json_polymorphic;

import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
import rai.json.json_field;

import rai.collection.sorted_hash_array_map;

namespace rai::json {

// ------------------------- Polymorphic helpers and fields -------------------------

/// @brief ポインタ型から要素型を抽出するメタ関数。
/// @tparam T ポインタ型（unique_ptr<T>、shared_ptr<T>、T*）。
template <typename T>
struct PointerElementType;

template <typename T>
struct PointerElementType<std::unique_ptr<T>> {
    using type = T;
};

template <typename T>
struct PointerElementType<std::shared_ptr<T>> {
    using type = T;
};

template <typename T>
struct PointerElementType<T*> {
    using type = T;
};

/// @brief ポリモーフィック型用のファクトリ関数型（ポインタ型を返す）。
export template <typename PtrType>
    requires IsSmartOrRawPointer<PtrType>
using PolymorphicTypeFactory = std::function<PtrType()>;

/// @brief ポリモーフィックオブジェクト1つ分を読み取るヘルパー関数。
/// @tparam PtrType ポインタ型（unique_ptr/shared_ptr/生ポインタ）。
/// @param parser JsonParserの参照。
/// @param entriesMap 型名からファクトリ関数へのマッピング。
/// @param jsonKey 型判別用のJSONキー名。
/// @return 読み取ったオブジェクトのポインタ。または型キーが見つからない／未知の型名の場合はnullptr。
export template <typename PtrType>
    requires IsSmartOrRawPointer<PtrType>
PtrType readPolymorphicInstance(
    JsonParser& parser,
    const collection::MapReference<std::string_view, PolymorphicTypeFactory<PtrType>>& entriesMap,
    std::string_view jsonKey = "type") {

    parser.startObject();

    // 最初のキーが型判別キーであることを確認
    std::string typeKey = parser.nextKey();
    if (typeKey != jsonKey) {
        throw std::runtime_error(
            std::string("Expected '") + std::string(jsonKey) +
            "' key for polymorphic object, got '" + typeKey + "'");
    }

    // 型名を読み取り、対応するファクトリを検索
    std::string typeName;
    parser.readTo(typeName);
    const auto* factory = entriesMap.findValue(typeName);
    if (!factory) {
        return nullptr;
    }

    // ファクトリでインスタンスを生成
    auto instance = (*factory)();
    using BaseType = typename PointerElementType<PtrType>::type;

    // HasJsonFieldsを持つ型の場合、残りのフィールドを読み取る
    if constexpr (HasJsonFields<BaseType>) {
        auto& fields = instance->jsonFields();
        BaseType* raw = std::to_address(instance);
        fields.readObject(parser, raw);
    }
    else {
        // jsonFieldsを持たない型の場合、全フィールドをスキップ
        while (!parser.nextIsEndObject()) {
            std::string key = parser.nextKey();
            parser.noteUnknownKey(key);
            parser.skipValue();
        }
    }

    parser.endObject();
    return instance;
}

/// @brief ポリモーフィックオブジェクト1つ分を読み取るヘルパー関数（null許容版）。
export template <typename PtrType>
    requires IsSmartOrRawPointer<PtrType>
PtrType readPolymorphicInstanceOrNull(
    JsonParser& parser,
    const collection::MapReference<std::string_view, PolymorphicTypeFactory<PtrType>>& entriesMap,
    std::string_view jsonKey = "type") {
    // null値の場合はnullptrを返す
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }
    // オブジェクトの場合は通常の読み取り処理
    auto position = parser.nextPosition();
    auto instance = readPolymorphicInstance<PtrType>(parser, entriesMap, jsonKey);
    if (instance == nullptr) {
         throw std::runtime_error("Unknown polymorphic type: " + std::to_string(position));
    }
    return instance;
}

// ヘルパ: entries マップを走査してオブジェクトの型名を取得します（ポリモーフィック書き出し時に使用）
export template <typename BaseType, typename Map>
std::string getTypeNameFromMap(const BaseType& obj, Map entries) {
    for (const auto& it : entries) {
        auto testObj = it.value();
        if (typeid(obj) == typeid(*testObj)) {
            return std::string(it.key);
        }
    }
    throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
}

// PolymorphicConverter: ポインタ型（unique_ptr/shared_ptr/生ポインタ）に対して IsJsonConverter を満たすコンバータ
export template <typename PtrType>
    requires IsSmartOrRawPointer<PtrType>
struct PolymorphicConverter {
    using Value = PtrType;
    using Element = typename PointerElementType<std::remove_cvref_t<Value>>::type;
    using Key = std::string_view;
    using Factory = PolymorphicTypeFactory<PtrType>;
    using Map = collection::MapReference<Key, Factory>;

    // Accept a MapReference-like object (SortedHashArrayMap is convertible)
    template <typename Entries>
    constexpr explicit PolymorphicConverter(const Entries& entries, const char* jsonKey = "type", bool allowNull = true)
        : entries_(entries), jsonKey_(jsonKey), allowNull_(allowNull) {}

    PtrType read(JsonParser& parser) const {
        if (allowNull_) {
            return readPolymorphicInstanceOrNull<PtrType>(parser, entries_, jsonKey_);
        }
        return readPolymorphicInstance<PtrType>(parser, entries_, jsonKey_);
    }

    void write(JsonWriter& writer, const PtrType& ptr) const {
        if (!ptr) {
            writer.null();
            return;
        }
        writer.startObject();
        std::string typeName = getTypeNameFromMap(*ptr, entries_);
        writer.key(jsonKey_);
        writer.writeObject(typeName);
        auto& fields = ptr->jsonFields();
        fields.writeFieldsOnly(writer, std::to_address(ptr));
        writer.endObject();
    }

private:
    Map entries_{};
    const char* jsonKey_{};
    bool allowNull_{true};
};

// Convenience factory: build a JsonField (with a static PolymorphicConverter) from an entries map
export template <typename MemberPtrType, typename MapType, bool Required = true>
constexpr auto makeJsonPolymorphicField(MemberPtrType memberPtr, const char* keyName,
    const MapType& entries, const char* jsonKey = "type")
    requires IsSmartOrRawPointer<MemberPointerValueType<MemberPtrType>> {
    using Value = MemberPointerValueType<MemberPtrType>;
    static const PolymorphicConverter<Value> conv(entries, jsonKey);
    if constexpr (Required) {
        using Behavior = RequiredFieldOmitBehavior<Value>;
        return JsonField<MemberPtrType, PolymorphicConverter<Value>, Behavior>(
            memberPtr, keyName, std::cref(conv), Behavior{});
    }
    else {
        using Behavior = DefaultFieldOmitBehavior<Value>;
        return JsonField<MemberPtrType, PolymorphicConverter<Value>, Behavior>(
            memberPtr, keyName, std::cref(conv), Behavior{});
    }
}

// Overload for SortedHashArrayMap-style entries
export template <typename MemberPtrType, size_t N, typename Traits, bool Required = true>
constexpr auto makeJsonPolymorphicField(MemberPtrType memberPtr, const char* keyName,
    const collection::SortedHashArrayMap<std::string_view,
        PolymorphicTypeFactory<MemberPointerValueType<MemberPtrType>>, N, Traits>& entries,
    const char* jsonKey = "type") {
    using MapRef = collection::MapReference<std::string_view,
        PolymorphicTypeFactory<MemberPointerValueType<MemberPtrType>>>;
    return makeJsonPolymorphicField<MemberPtrType, MapRef, Required>(
        memberPtr, keyName, MapRef(entries), jsonKey);
} 

// JsonPolymorphicField and JsonPolymorphicArrayField have been removed. Use the converter-based helpers (makeJsonPolymorphicField / makeJsonPolymorphicArrayField) instead.

/// @brief ポリモーフィックな配列（vector<std::unique_ptr<BaseType>>）用のファクトリ helper
export template <typename MemberPtrType, typename MapType, bool Required = true>
constexpr auto makeJsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName,
    const MapType& entries, const char* jsonKey = "type") {
    using Container = MemberPointerValueType<MemberPtrType>;
    using ElementPtr = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static const PolymorphicConverter<ElementPtr> elemConv(entries, jsonKey);
    using ContainerConv = ContainerConverter<Container, PolymorphicConverter<ElementPtr>>;
    static const ContainerConv conv(elemConv);
    if constexpr (Required) {
        using Behavior = RequiredFieldOmitBehavior<Container>;
        return JsonField<MemberPtrType, ContainerConv, Behavior>(
            memberPtr, keyName, std::cref(conv), Behavior{});
    }
    else {
        using Behavior = DefaultFieldOmitBehavior<Container>;
        return JsonField<MemberPtrType, ContainerConv, Behavior>(
            memberPtr, keyName, std::cref(conv), Behavior{});
    }
}
// Convenience factory: build a JsonField for a container of polymorphic pointer elements
export template <typename MemberPtrType, typename MapType, bool Required = true>
constexpr auto makeJsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName,
    const MapType& entries, const char* jsonKey = "type")
    requires IsContainer<MemberPointerValueType<MemberPtrType>>
    && IsSmartOrRawPointer<std::remove_cvref_t<std::ranges::range_value_t<
        MemberPointerValueType<MemberPtrType>>>> {
    using Container = MemberPointerValueType<MemberPtrType>;
    using ElementPtr = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static const PolymorphicConverter<ElementPtr> elemConv(entries, jsonKey);
    using ContainerConv = ContainerConverter<Container, PolymorphicConverter<ElementPtr>>;
    static const ContainerConv conv(elemConv);
    if constexpr (Required) {
        using Behavior = RequiredFieldOmitBehavior<Container>;
        return JsonField<MemberPtrType, ContainerConv, Behavior>(
            memberPtr, keyName, std::cref(conv), Behavior{});
    }
    else {
        using Behavior = DefaultFieldOmitBehavior<Container>;
        return JsonField<MemberPtrType, ContainerConv, Behavior>(
            memberPtr, keyName, std::cref(conv), Behavior{});
    }
}



} // namespace rai::json