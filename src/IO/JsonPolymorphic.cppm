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

export module rai.serialization.json_polymorphic;

import rai.serialization.json_writer;
import rai.serialization.json_parser;
import rai.serialization.json_token_manager;
import rai.serialization.json_converter;

import rai.collection.sorted_hash_array_map;

namespace rai::serialization {

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

/// @brief std::shared_ptr を判定する concept（element_type を確認し正確に判定）。
/// @tparam T 判定対象の型。
template <typename T>
concept IsSharedPtr = requires {
    typename T::element_type;
} && std::is_same_v<T, std::shared_ptr<typename T::element_type>>;

/// @brief ポインタ型（unique_ptr/shared_ptr/生ポインタ）であることを確認する concept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsSmartOrRawPointer = IsUniquePtr<T> || IsSharedPtr<T> || std::is_pointer_v<T>;

/// @brief ポリモーフィック型用のファクトリ関数型（ポインタ型を返す）。
export template <typename Ptr>
    requires IsSmartOrRawPointer<Ptr>
using PolymorphicTypeFactory = std::function<Ptr()>;

/// @brief ポリモーフィックオブジェクト1つ分を読み取るヘルパー関数。
/// @tparam Ptr ポインタ型（unique_ptr/shared_ptr/生ポインタ）。
/// @param parser JsonParserの参照。
/// @param entriesMap 型名からファクトリ関数へのマッピング。
/// @param jsonKey 型判別用のJSONキー名。
/// @return 読み取ったオブジェクトのポインタ。または型キーが見つからない／未知の型名の場合はnullptr。
export template <typename Ptr>
    requires IsSmartOrRawPointer<Ptr>
Ptr readPolymorphicInstance(
    JsonParser& parser,
    const collection::MapReference<std::string_view, PolymorphicTypeFactory<Ptr>>& entriesMap,
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
    using BaseType = typename PointerElementType<Ptr>::type;

    // HasJsonFieldsを持つ型の場合、残りのフィールドを読み取る
    if constexpr (HasJsonFields<BaseType>) {
        auto& fields = instance->jsonFields();
        BaseType* raw = std::to_address(instance);
        fields.readFields(parser, raw);
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
export template <typename Ptr>
    requires IsSmartOrRawPointer<Ptr>
Ptr readPolymorphicInstanceOrNull(
    JsonParser& parser,
    const collection::MapReference<std::string_view, PolymorphicTypeFactory<Ptr>>& entriesMap,
    std::string_view jsonKey = "type") {
    // null値の場合はnullptrを返す
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }
    // オブジェクトの場合は通常の読み取り処理
    auto position = parser.nextPosition();
    auto instance = readPolymorphicInstance<Ptr>(parser, entriesMap, jsonKey);
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
export template <typename Ptr>
    requires IsSmartOrRawPointer<Ptr>
struct PolymorphicConverter {
    using Value = Ptr;
    using Element = typename PointerElementType<std::remove_cvref_t<Value>>::type;
    using Key = std::string_view;
    using Factory = PolymorphicTypeFactory<Ptr>;
    using Map = collection::MapReference<Key, Factory>;

    // Accept a MapReference-like object (SortedHashArrayMap is convertible)
    template <typename Entries>
    constexpr explicit PolymorphicConverter(
        const Entries& entries, const char* jsonKey = "type", bool allowNull = true)
        : entries_(entries), jsonKey_(jsonKey), allowNull_(allowNull) {}

    Ptr read(JsonParser& parser) const {
        if (allowNull_) {
            return readPolymorphicInstanceOrNull<Ptr>(parser, entries_, jsonKey_);
        }
        return readPolymorphicInstance<Ptr>(parser, entries_, jsonKey_);
    }

    void write(JsonWriter& writer, const Ptr& ptr) const {
        if (!ptr) {
            writer.null();
            return;
        }
        writer.startObject();
        std::string typeName = getTypeNameFromMap(*ptr, entries_);
        writer.key(jsonKey_);
        writer.writeObject(typeName);
        auto& fields = ptr->jsonFields();
        fields.writeFields(writer, std::to_address(ptr));
        writer.endObject();
    }

private:
    Map entries_{};
    const char* jsonKey_{};
    bool allowNull_{true};
};

/// @brief ポリモーフィック型用のコンバータを構築して返す。
/// @tparam Ptr ポインタ型（unique_ptr/shared_ptr/生ポインタ）
/// @param entries 型名からファクトリ関数へのマップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Ptr, typename Map>
    requires IsSmartOrRawPointer<Ptr>
constexpr auto getPolymorphicConverter(
    const Map& entries, const char* jsonKey = "type", bool allowNull = true) {
    return PolymorphicConverter<Ptr>(entries, jsonKey, allowNull);
}

/// @brief ポリモーフィックな配列用のコンバータを構築して返す。
/// @tparam Container ポインタ要素を持つコンテナ型
/// @param entries 型名からファクトリ関数へのマップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Container, typename Map>
    requires IsContainer<Container>
    && IsSmartOrRawPointer<std::remove_cvref_t<std::ranges::range_value_t<Container>>>
constexpr auto getPolymorphicArrayConverter(
    const Map& entries, const char* jsonKey = "type", bool allowNull = true) {
    using ElementPtr = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static const auto elementConverter =
        getPolymorphicConverter<ElementPtr>(entries, jsonKey, allowNull);
    using ElementConverter = std::remove_cvref_t<decltype(elementConverter)>;
    return ContainerConverter<Container, ElementConverter>(elementConverter);
}



} // namespace rai::serialization