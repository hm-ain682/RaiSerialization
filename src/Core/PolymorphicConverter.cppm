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
#include <typeindex>
#include <vector>
#include <set>
#include <unordered_set>

export module rai.serialization.core:polymorphic_converter;

import :object_converter;
import :container_converter;
import :object_serializer;
import rai.collection.sorted_hash_array_map;
import rai.serialization.token_manager;
import rai.serialization.json;

namespace rai::serialization {

// ------------------------- Polymorphic helpers and fields -------------------------

/// @brief ポリモーフィック型用のファクトリ関数型（ポインタ型を返す）。
export template <typename Ptr>
    requires IsPointerLike<Ptr>
using PolymorphicTypeFactory = std::function<Ptr()>;

/// @brief serializer() を持たないポリモーフィック型を外部 ObjectSerializer で扱うための登録情報。
/// @details type は書き込み時に実際の派生型から型タグを逆引きするために使う。
export template <typename Ptr>
    requires IsPointerLike<Ptr>
struct PolymorphicSerializerEntry {
    using Factory = PolymorphicTypeFactory<Ptr>;

    std::type_index type;
    Factory factory;
    std::reference_wrapper<const ObjectSerializer> serializer;
};

/// @brief ポリモーフィックオブジェクト1つ分を読み取るヘルパー関数。
/// @tparam Ptr ポインタ型（unique_ptr/shared_ptr/生ポインタ）。
/// @param parser JsonParserの参照。
/// @param entriesMap 型名からファクトリ関数へのマッピング。
/// @param jsonKey 型判別用のJSONキー名。
/// @return 読み取ったオブジェクトのポインタ。または型キーが見つからない／未知の型名の場合はnullptr。
export template <typename Ptr, typename EntryValue = PolymorphicTypeFactory<Ptr>>
    requires IsPointerLike<Ptr>
Ptr readPolymorphicInstance(JsonParser& parser,
    const collection::MapReference<std::string_view, EntryValue>& entriesMap,
    std::string_view jsonKey) {

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
    const auto* entry = entriesMap.findValue(typeName);
    if (!entry) {
        return nullptr;
    }

    // ファクトリでインスタンスを生成
    // factory だけの従来エントリでは既存の serializer() ベース動作を維持する。
    // 外部 serializer エントリでは、同じく factory で生成してから登録済み
    // ObjectSerializer でフィールドを読み込む。
    auto instance = [&]() {
        if constexpr (std::same_as<EntryValue, PolymorphicTypeFactory<Ptr>>) {
            return (*entry)();
        }
        else {
            return entry->factory();
        }
    }();
    using BaseType = typename PointerElementType<Ptr>::type;

    // 外部 serializer エントリは serializer() を公開しない型向け。
    // 従来パスは既存の polymorphic map の動作を保つため、基底オブジェクトの
    // 仮想 serializer() を使う。
    BaseType* raw = getRawPointer(instance);
    if constexpr (std::same_as<EntryValue, PolymorphicSerializerEntry<Ptr>>) {
        entry->serializer.get().readFields(parser, raw);
    }
    else if constexpr (HasSerializer<BaseType>) {
        auto& fields = raw->serializer();
        fields.readFields(parser, raw);
    }
    else {
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
export template <typename Ptr, typename EntryValue = PolymorphicTypeFactory<Ptr>>
    requires IsPointerLike<Ptr>
Ptr readPolymorphicInstanceOrNull(JsonParser& parser,
    const collection::MapReference<std::string_view, EntryValue>& entriesMap,
    std::string_view jsonKey) {
    // null値の場合はnullptrを返す
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }
    // オブジェクトの場合は通常の読み取り処理
    auto position = parser.nextPosition();
    auto instance = readPolymorphicInstance<Ptr, EntryValue>(parser, entriesMap, jsonKey);
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

template <typename BaseType, typename Ptr>
std::string getTypeNameFromMap(const BaseType& obj,
    collection::MapReference<std::string_view, PolymorphicSerializerEntry<Ptr>> entries) {
    // 新方式のエントリは具象型を明示的に保持するため、書き込み時に
    // typeid 比較用の一時オブジェクトを生成しなくてよい。
    const std::type_index actualType{typeid(obj)};
    for (const auto& it : entries) {
        if (actualType == it.value.type) {
            return std::string(it.key);
        }
    }
    throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
}

// PolymorphicConverter: ポインタ型（unique_ptr/shared_ptr/生ポインタ）に対して IsObjectConverter を満たすコンバータ
export template <typename Ptr, typename EntryValue = PolymorphicTypeFactory<Ptr>>
    requires IsPointerLike<Ptr>
struct PolymorphicConverter {
    using Value = Ptr;
    using Element = typename PointerElementType<std::remove_cvref_t<Value>>::type;
    using Key = std::string_view;
    using Factory = PolymorphicTypeFactory<Ptr>;
    using Entry = EntryValue;
    using Map = collection::MapReference<Key, Entry>;

    // Accept a MapReference-like object (SortedHashArrayMap is convertible)
    template <typename Entries>
    constexpr explicit PolymorphicConverter(
        const Entries& entries, const char* jsonKey = "type", bool allowNull = true)
        : entries_(entries), jsonKey_(jsonKey), allowNull_(allowNull) {}

    void read(JsonParser& parser, Ptr& out) const {
        if (allowNull_) {
            out = readPolymorphicInstanceOrNull<Ptr, Entry>(parser, entries_, jsonKey_);
            return;
        }
        out = readPolymorphicInstance<Ptr, Entry>(parser, entries_, jsonKey_);
    }

    void write(JsonWriter& writer, const Ptr& ptr) const {
        if (ptr == nullptr) {
            writer.null();
            return;
        }
        writer.startObject();
        std::string typeName = getTypeNameFromMap(*ptr, entries_);
        writer.key(jsonKey_);
        writer.writeObject(typeName);
        Element* raw = getRawPointer(ptr);
        if constexpr (std::same_as<Entry, PolymorphicSerializerEntry<Ptr>>) {
            // 指し先オブジェクトに serializer() を要求せず、型に一致した
            // 登録エントリの serializer を使う。
            const auto actualType = std::type_index(typeid(*ptr));
            for (const auto& it : entries_) {
                if (actualType == it.value.type) {
                    it.value.serializer.get().writeFields(writer, raw);
                    writer.endObject();
                    return;
                }
            }
            throw std::runtime_error(
                "PolymorphicConverter::write: serializer is not provided for polymorphic object");
        }
        else if constexpr (HasSerializer<Element>) {
            auto& fields = raw->serializer();
            fields.writeFields(writer, raw);
        }
        else {
            throw std::runtime_error(
                "PolymorphicConverter::write: serializer is not provided for polymorphic object");
        }
        writer.endObject();
    }

private:
    Map entries_{};
    const char* jsonKey_{};
    bool allowNull_{true};
};

/// @brief ポリモーフィック型用のコンバータを構築して返す。
/// @tparam Ptr ポインタ型（unique_ptr/shared_ptr/生ポインタ）
/// @param entries 型名からファクトリ関数(PolymorphicTypeFactory<Ptr>)へのマップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Ptr, typename Map>
    requires IsPointerLike<Ptr>
constexpr auto getPolymorphicConverter(
    const Map& entries, const char* jsonKey = "type", bool allowNull = true) {
    return PolymorphicConverter<Ptr>(entries, jsonKey, allowNull);
}

/// @brief ポリモーフィック型用のコンバータを構築して返す。
/// @tparam Ptr ポインタ型（unique_ptr/shared_ptr/生ポインタ）
/// @param entries 型名から PolymorphicSerializerEntry<Ptr> へのマップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Ptr, std::size_t N, typename Traits>
    requires IsPointerLike<Ptr>
constexpr auto getPolymorphicConverter(
    const collection::SortedHashArrayMap<
        std::string_view, PolymorphicSerializerEntry<Ptr>, N, Traits>& entries,
    const char* jsonKey = "type", bool allowNull = true) {
    // 値が ObjectSerializer を含む map 用の overload。
    return PolymorphicConverter<Ptr, PolymorphicSerializerEntry<Ptr>>(
        entries, jsonKey, allowNull);
}

/// @brief MapReference で渡された外部 serializer 登録からポリモーフィック型用コンバータを構築する。
/// @tparam Ptr ポインタ型（unique_ptr/shared_ptr/生ポインタ）
/// @param entries 型名から PolymorphicSerializerEntry<Ptr> への参照マップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Ptr, typename Traits>
    requires IsPointerLike<Ptr>
constexpr auto getPolymorphicConverter(
    const collection::MapReference<
        std::string_view, PolymorphicSerializerEntry<Ptr>, Traits>& entries,
    const char* jsonKey = "type", bool allowNull = true) {
    // SortedHashArrayMap を保持しない呼び出し側にも同じ外部 serializer パスを提供する。
    return PolymorphicConverter<Ptr, PolymorphicSerializerEntry<Ptr>>(
        entries, jsonKey, allowNull);
}

/// @brief ポリモーフィックな配列用のコンバータを構築して返す。
/// @tparam Container ポインタ要素を持つコンテナ型
/// @param entries 型名からファクトリ関数へのマップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Container, typename Map>
    requires IsContainer<Container>
    && IsPointerLike<std::remove_cvref_t<std::ranges::range_value_t<Container>>>
constexpr auto getPolymorphicArrayConverter(
    const Map& entries, const char* jsonKey = "type", bool allowNull = true) {
    using ElementPtr = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static const auto elementConverter =
        getPolymorphicConverter<ElementPtr>(entries, jsonKey, allowNull);
    using ElementConverter = std::remove_cvref_t<decltype(elementConverter)>;
    return ContainerConverter<Container, ElementConverter>(elementConverter);
}

/// @brief 外部 serializer 登録を使うポリモーフィック配列用コンバータを構築して返す。
/// @tparam Container ポインタ要素を持つコンテナ型
/// @param entries 型名から要素ポインタ用 PolymorphicSerializerEntry へのマップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Container, std::size_t N, typename Traits>
    requires IsContainer<Container>
    && IsPointerLike<std::remove_cvref_t<std::ranges::range_value_t<Container>>>
constexpr auto getPolymorphicArrayConverter(
    const collection::SortedHashArrayMap<
        std::string_view,
        PolymorphicSerializerEntry<std::remove_cvref_t<std::ranges::range_value_t<Container>>>,
        N,
        Traits>& entries,
    const char* jsonKey = "type", bool allowNull = true) {
    using ElementPtr = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static const auto elementConverter =
        getPolymorphicConverter<ElementPtr>(entries, jsonKey, allowNull);
    using ElementConverter = std::remove_cvref_t<decltype(elementConverter)>;
    return ContainerConverter<Container, ElementConverter>(elementConverter);
}

/// @brief MapReference で渡された外部 serializer 登録を使うポリモーフィック配列用コンバータを構築して返す。
/// @tparam Container ポインタ要素を持つコンテナ型
/// @param entries 型名から要素ポインタ用 PolymorphicSerializerEntry への参照マップ
/// @param jsonKey 型判別用のJSONキー名
/// @param allowNull null許容かどうか
export template <typename Container, typename Traits>
    requires IsContainer<Container>
    && IsPointerLike<std::remove_cvref_t<std::ranges::range_value_t<Container>>>
constexpr auto getPolymorphicArrayConverter(
    const collection::MapReference<
        std::string_view,
        PolymorphicSerializerEntry<std::remove_cvref_t<std::ranges::range_value_t<Container>>>,
        Traits>& entries,
    const char* jsonKey = "type", bool allowNull = true) {
    using ElementPtr = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static const auto elementConverter =
        getPolymorphicConverter<ElementPtr>(entries, jsonKey, allowNull);
    using ElementConverter = std::remove_cvref_t<decltype(elementConverter)>;
    return ContainerConverter<Container, ElementConverter>(elementConverter);
}



} // namespace rai::serialization
