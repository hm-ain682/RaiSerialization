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

/// @brief 読み込み時に具象ポリモーフィックオブジェクトを生成する factory 型。
/// @details JsonParser を受け取るため、factory 内で parser.context<T>() などの読み込み時状態を参照できる。
export template <typename Ptr>
    requires IsPointerLike<Ptr>
using PolymorphicTypeFactory = std::function<Ptr(JsonParser&)>;

/// @brief serializer() を公開するポリモーフィック型の登録エントリ。
/// @details typeName は JSON 上の型名、type は書き込み時の逆引き、factory は読み込み時の生成に使う。
export template <typename Ptr>
    requires IsPointerLike<Ptr>
struct PolymorphicTypeEntry {
    using Factory = PolymorphicTypeFactory<Ptr>;

    /// @brief 型判別フィールドに読み書きする JSON 上の型名。
    std::string_view typeName;
    /// @brief 書き込み時に具象 C++ 型から typeName を引くための型情報。
    std::type_index type;
    /// @brief JSON の typeName が一致したときにインスタンスを生成する factory。
    Factory factory;
};

/// @brief 外部 ObjectSerializer で扱うポリモーフィック型の登録エントリ。
/// @details 具象型が serializer() を公開しない場合に使う。
export template <typename Ptr>
    requires IsPointerLike<Ptr>
struct PolymorphicSerializerEntry {
    using Factory = PolymorphicTypeFactory<Ptr>;

    /// @brief 型判別フィールドに読み書きする JSON 上の型名。
    std::string_view typeName;
    /// @brief 書き込み時に具象 C++ 型から typeName と serializer を引くための型情報。
    std::type_index type;
    /// @brief JSON の typeName が一致したときにインスタンスを生成する factory。
    Factory factory;
    /// @brief 具象オブジェクトのフィールドを読み書きする外部 serializer。
    std::reference_wrapper<const ObjectSerializer> serializer;
};

/// @brief 呼び出し側に std::type_index を書かせず PolymorphicTypeEntry を作る。
/// @param typeName JSON 上の型名。
/// @param factory JsonParser* を受け取りポインタ風の値を返す callable。
export template <typename Concrete, typename Factory>
auto makePolymorphicTypeEntry(std::string_view typeName, Factory&& factory) {
    using Ptr = std::invoke_result_t<Factory&, JsonParser&>;
    return PolymorphicTypeEntry<Ptr>{
        typeName,
        std::type_index(typeid(Concrete)),
        PolymorphicTypeFactory<Ptr>(std::forward<Factory>(factory))
    };
}

/// @brief 呼び出し側に std::type_index を書かせず PolymorphicSerializerEntry を作る。
/// @param typeName JSON 上の型名。
/// @param factory JsonParser* を受け取りポインタ風の値を返す callable。
/// @param serializer 具象型を読み書きする外部 serializer。
export template <typename Concrete, typename Factory>
auto makePolymorphicSerializerEntry(std::string_view typeName, Factory&& factory,
    const ObjectSerializer& serializer) {
    using Ptr = std::invoke_result_t<Factory&, JsonParser&>;
    return PolymorphicSerializerEntry<Ptr>{
        typeName,
        std::type_index(typeid(Concrete)),
        PolymorphicTypeFactory<Ptr>(std::forward<Factory>(factory)),
        serializer
    };
}

/// @brief std::type_index を SortedHashArrayMap のキーにするための traits。
struct TypeIndexMapTraits {
    struct Hash {
        std::size_t operator()(std::type_index type) const {
            return type.hash_code();
        }
    };

    using KeyEqual = std::equal_to<>;
    using KeyCompare = std::less<>;
};

/// @brief 登録エントリを「typeName -> entry」の map 要素へ変換する。
template <typename Entry>
std::pair<std::string_view, Entry> makeTypeNameEntry(const Entry& entry) {
    return {entry.typeName, entry};
}

/// @brief 登録エントリを「具象型 -> typeName」の map 要素へ変換する。
template <typename Entry>
std::pair<std::type_index, std::string_view> makeTypeIndexEntry(const Entry& entry) {
    return {entry.type, entry.typeName};
}

/// @brief 登録配列から読み込み用の「typeName -> entry」map を構築する。
template <typename Entry, std::size_t N, std::size_t... Is>
auto makePolymorphicEntriesMapImpl(
    const std::array<Entry, N>& entries, std::index_sequence<Is...>) {
    return collection::SortedHashArrayMap<std::string_view, Entry, N>(
        makeTypeNameEntry(entries[Is])...);
}

/// @brief 登録配列から読み込み用の「typeName -> entry」map を構築する。
template <typename Entry, std::size_t N>
auto makePolymorphicEntriesMap(const std::array<Entry, N>& entries) {
    return makePolymorphicEntriesMapImpl(entries, std::make_index_sequence<N>{});
}

/// @brief 登録配列から書き込み用の「具象型 -> typeName」map を構築する。
template <typename Entry, std::size_t N, std::size_t... Is>
auto makePolymorphicTypeMapImpl(
    const std::array<Entry, N>& entries, std::index_sequence<Is...>) {
    return collection::SortedHashArrayMap<
        std::type_index, std::string_view, N, TypeIndexMapTraits>(
        makeTypeIndexEntry(entries[Is])...);
}

/// @brief 登録配列から書き込み用の「具象型 -> typeName」map を構築する。
template <typename Entry, std::size_t N>
auto makePolymorphicTypeMap(const std::array<Entry, N>& entries) {
    return makePolymorphicTypeMapImpl(entries, std::make_index_sequence<N>{});
}

/// @brief ポリモーフィックオブジェクトを 1 つ読み込む。
/// @details 先頭フィールドを型判別キーとして読み、対応する entry の factory でインスタンスを生成する。
export template <typename Ptr, typename Entry, std::size_t N>
    requires IsPointerLike<Ptr>
Ptr readPolymorphicInstance(JsonParser& parser,
    const collection::SortedHashArrayMap<std::string_view, Entry, N>& entriesMap,
    std::string_view jsonKey) {

    parser.startObject();

    std::string typeKey = parser.nextKey();
    if (typeKey != jsonKey) {
        throw std::runtime_error(
            std::string("Expected '") + std::string(jsonKey) +
            "' key for polymorphic object, got '" + typeKey + "'");
    }

    std::string typeName;
    parser.readTo(typeName);
    const auto* entry = entriesMap.findValue(typeName);
    if (!entry) {
        return nullptr;
    }

    auto instance = entry->factory(parser);
    using BaseType = typename PointerElementType<Ptr>::type;
    BaseType* raw = getRawPointer(instance);

    if constexpr (std::same_as<Entry, PolymorphicSerializerEntry<Ptr>>) {
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

/// @brief null を許容してポリモーフィックオブジェクトを 1 つ読み込む。
/// @details 次のトークンが null なら nullptr を返し、それ以外は readPolymorphicInstance に委譲する。
export template <typename Ptr, typename Entry, std::size_t N>
    requires IsPointerLike<Ptr>
Ptr readPolymorphicInstanceOrNull(JsonParser& parser,
    const collection::SortedHashArrayMap<std::string_view, Entry, N>& entriesMap,
    std::string_view jsonKey) {
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }
    auto position = parser.nextPosition();
    auto instance = readPolymorphicInstance<Ptr, Entry, N>(parser, entriesMap, jsonKey);
    if (instance == nullptr) {
         throw std::runtime_error("Unknown polymorphic type: " + std::to_string(position));
    }
    return instance;
}

/// @brief ポインタ風のポリモーフィック値を読み書きする converter。
/// @details 読み込み用の「typeName -> entry」と、書き込み用の「具象型 -> typeName」の 2 つの SortedHashArrayMap を保持する。
export template <typename Ptr, typename Entry, std::size_t N>
    requires IsPointerLike<Ptr>
struct PolymorphicConverter {
    using Value = Ptr;
    using Element = typename PointerElementType<std::remove_cvref_t<Value>>::type;
    using EntriesMap = collection::SortedHashArrayMap<std::string_view, Entry, N>;
    using TypeMap = collection::SortedHashArrayMap<
        std::type_index, std::string_view, N, TypeIndexMapTraits>;

    explicit PolymorphicConverter(
        const std::array<Entry, N>& entries,
        const char* jsonKey = "type", bool allowNull = true)
        : entries_(makePolymorphicEntriesMap(entries)),
          typeNames_(makePolymorphicTypeMap(entries)),
          jsonKey_(jsonKey),
          allowNull_(allowNull) {}

    /// @brief ポインタ風のポリモーフィック値を読み込む。
    std::size_t read(JsonParser& parser, Ptr& out) const {
        if (allowNull_) {
            out = readPolymorphicInstanceOrNull<Ptr, Entry, N>(parser, entries_, jsonKey_);
            return parser.nextPosition();
        }
        out = readPolymorphicInstance<Ptr, Entry, N>(parser, entries_, jsonKey_);
        return parser.nextPosition();
    }

    /// @brief ポインタ風のポリモーフィック値を書き出す。
    /// @details factory は呼ばず、typeid(*ptr) と typeNames_ から JSON 上の型名を引く。
    void write(JsonWriter& writer, const Ptr& ptr) const {
        if (ptr == nullptr) {
            writer.null();
            return;
        }

        const auto actualType = std::type_index(typeid(*ptr));
        const auto* typeName = typeNames_.findValue(actualType);
        if (typeName == nullptr) {
            throw std::runtime_error(
                std::string("Unknown polymorphic type: ") + typeid(*ptr).name());
        }

        writer.startObject();
        writer.key(jsonKey_);
        writer.writeObject(std::string(*typeName));

        Element* raw = getRawPointer(ptr);
        if constexpr (std::same_as<Entry, PolymorphicSerializerEntry<Ptr>>) {
            const auto* entry = entries_.findValue(*typeName);
            if (entry == nullptr) {
                throw std::runtime_error(
                    "PolymorphicConverter::write: serializer is not provided for polymorphic object");
            }
            entry->serializer.get().writeFields(writer, raw);
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
    /// @brief 読み込み時に JSON 上の型名から登録エントリを引く map。
    EntriesMap entries_;
    /// @brief 書き込み時に具象 C++ 型から JSON 上の型名を引く map。
    TypeMap typeNames_;
    /// @brief JSON 上の型判別フィールド名。
    const char* jsonKey_{};
    /// @brief null を nullptr として扱うかどうか。
    bool allowNull_{true};
};

/// @brief ポインタ風のポリモーフィック値用 converter を作る。
export template <typename Ptr, typename Entry, std::size_t N>
    requires IsPointerLike<Ptr>
auto getPolymorphicConverter(
    const std::array<Entry, N>& entries,
    const char* jsonKey = "type", bool allowNull = true) {
    return PolymorphicConverter<Ptr, Entry, N>(entries, jsonKey, allowNull);
}

/// @brief ポインタ風のポリモーフィック値を要素に持つコンテナ用 converter を作る。
export template <typename Container, typename Entry, std::size_t N>
    requires IsContainer<Container>
    && IsPointerLike<std::remove_cvref_t<std::ranges::range_value_t<Container>>>
auto getPolymorphicArrayConverter(
    const std::array<Entry, N>& entries,
    const char* jsonKey = "type", bool allowNull = true) {
    using ElementPtr = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    const auto elementConverter =
        getPolymorphicConverter<ElementPtr>(entries, jsonKey, allowNull);
    using ElementConverter = std::remove_cvref_t<decltype(elementConverter)>;
    return ContainerConverter<Container, ElementConverter>(elementConverter);
}


} // namespace rai::serialization
