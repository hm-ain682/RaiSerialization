/// @file JsonField.cppm
/// @brief JSONフィールドの定義。構造体とJSONの相互変換を提供する。

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

export module rai.json.json_field;

import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
import rai.json.json_value_io;
import rai.collection.sorted_hash_array_map;

namespace rai::json {

// ******************************************************************************** メタプログラミング用の型特性

/// @brief メンバーポインタの特性を抽出するメタ関数。
/// @tparam T メンバーポインタ型。
export template <typename T>
struct MemberPointerTraits;

template <typename Owner, typename Value>
struct MemberPointerTraits<Value Owner::*> {
    using OwnerType = Owner;
    using ValueType = Value;
};

// 型エイリアス: メンバーポインタから ValueType を簡単に取り出す。
export template <typename MemberPtrType>
using MemberPointerValueType = typename MemberPointerTraits<MemberPtrType>::ValueType;

// ******************************************************************************** 共通基底

// JsonEnumMapのように、enum <-> 文字列名の双方向マップを提供する型のconcept。
export template <typename Converter, typename Value>
concept IsJsonConverter
    = requires { typename Converter::Value; }
    && std::is_same_v<typename Converter::Value, Value>
    && requires(const Converter& converter, JsonWriter& writer, const Value& value) {
        converter.write(writer, value);
    }
    && requires(const Converter& converter, JsonParser& parser) {
        { converter.read(parser) } -> std::same_as<Value>;
    };


// ******************************************************************************** 共通基底

// Note: JsonFieldBase was removed; use `JsonField<MemberPtrType, Converter>` and converter helpers
// (e.g., `makeJsonField`, `makeJsonPolymorphicField`, `makeJsonPolymorphicArrayField`) instead.


// ******************************************************************************** 基本型用



// JsonField helper moved: see later makeJsonField implementation that selects
// appropriate converter or falls back to JsonField for value_io-supported types.

// -----------------------------------------------------------------------------
// 新: JsonField とコンバータ群（既存の IsJsonConverter に合わせる）
// -----------------------------------------------------------------------------

/// @brief 新しいフィールド型（変換器を外部から渡す）
export template <typename MemberPtrType, typename Converter>
    requires std::is_member_object_pointer_v<MemberPtrType>
        && IsJsonConverter<Converter, MemberPointerValueType<MemberPtrType>>
struct JsonField {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using OwnerType = typename Traits::OwnerType;
    using ValueType = typename Traits::ValueType;

    // Construct by reference: the converter is owned/managed externally and must outlive the field
    constexpr explicit JsonField(MemberPtrType memberPtr, const char* keyName,
        std::reference_wrapper<const Converter> conv, bool req = false)
        : member(memberPtr), converterRef(conv), key(keyName), required(req) {}

    // Convenience: accept a const Converter& and store a reference (caller must ensure lifetime)
    constexpr explicit JsonField(MemberPtrType memberPtr, const char* keyName,
        const Converter& conv, bool req = false)
        : member(memberPtr), converterRef(std::cref(conv)), key(keyName), required(req) {}

    MemberPtrType member{};                                   ///< メンバポインタ
    std::reference_wrapper<const Converter> converterRef;     ///< 値変換器への参照（必ず有効であること）
    const char* key{};                                        ///< JSONキー名
    bool required{false};                                     ///< 必須か

    void write(JsonWriter& writer, const ValueType& value) const {
        converterRef.get().write(writer, value);
    }

    ValueType read(JsonParser& parser) const {
        return converterRef.get().read(parser);
    }

    // For backward compatibility with existing code that expects toJson/fromJson
    void toJson(JsonWriter& writer, const ValueType& value) const { write(writer, value); }
    ValueType fromJson(JsonParser& parser) const { return read(parser); }
};

// makeJsonField has been moved below the converter definitions to ensure all converter
// template types are declared before use. See implementation later in this file.

// --- 基本的なコンバータ群（既存の value_io を利用する薄いラッパー）

/// @brief 基本型等を扱うコンバータ（value_io に委譲）
export template <typename T>
    requires (IsFundamentalValue<T> || std::same_as<T, std::string>)
struct FundamentalConverter {
    using Value = T;
    void write(JsonWriter& writer, const T& value) const { value_io::writeValue<T>(writer, value); }
    T read(JsonParser& parser) const { return value_io::readValue<T>(parser); }
};

/// @brief jsonFields を持つ型のコンバータ
export template <typename T>
    requires HasJsonFields<T> && std::default_initializable<T>
struct JsonFieldsConverter {
    using Value = T;
    void write(JsonWriter& writer, const T& obj) const {
        auto& fields = obj.jsonFields();
        writer.startObject();
        fields.writeFieldsOnly(writer, static_cast<const void*>(&obj));
        writer.endObject();
    }
    T read(JsonParser& parser) const {
        T obj{};
        auto& fields = obj.jsonFields();
        parser.startObject();
        while (!parser.nextIsEndObject()) {
            std::string key = parser.nextKey();
            if (!fields.readFieldByKey(parser, &obj, key)) {
                parser.noteUnknownKey(key);
                parser.skipValue();
            }
        }
        parser.endObject();
        return obj;
    }
};

/// @brief writeJson/readJson を持つ型のコンバータ
export template <typename T>
    requires HasReadJson<T> && HasWriteJson<T> && std::default_initializable<T>
struct WriteReadJsonConverter {
    using Value = T;
    void write(JsonWriter& writer, const T& obj) const { obj.writeJson(writer); }
    T read(JsonParser& parser) const { T out{}; out.readJson(parser); return out; }
};

/// @brief unique_ptr 等のコンバータ
export template <typename T>
    requires IsUniquePtr<T>
struct UniquePtrConverter {
    using Value = T;
    using Element = typename T::element_type;
    void write(JsonWriter& writer, const T& ptr) const {
        if (!ptr) { writer.null(); return; }
        value_io::writeValue<Element>(writer, *ptr);
    }
    T read(JsonParser& parser) const {
        if (parser.nextIsNull()) {
            parser.skipValue();
            return nullptr;
        }
        auto elem = value_io::readValue<Element>(parser);
        return std::make_unique<Element>(std::move(elem));
    }
};

/// @brief std::variant のコンバータ（value_io に委譲）
export template <typename T>
    requires IsStdVariant<T>
struct VariantConverter {
    using Value = T;
    void write(JsonWriter& writer, const T& v) const { value_io::writeValue<T>(writer, v); }
    T read(JsonParser& parser) const { return value_io::readValue<T>(parser); }
};

/// @brief コンテナ用コンバータ（要素コンバータ参照を持つ）。
export template <typename Container, typename ElementConverter>
    requires IsContainer<Container> && IsJsonConverter<ElementConverter, std::remove_cvref_t<std::ranges::range_value_t<Container>>>
struct ContainerConverter {
    using Value = Container;
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static_assert(std::is_same_v<typename ElementConverter::Value, Element>, "ElementConverter::Value must match container element type");

    constexpr explicit ContainerConverter(const ElementConverter& elemConv)
        : elemConvRef_(std::cref(elemConv)) {}

    void write(JsonWriter& writer, const Container& range) const {
        writer.startArray();
        for (const auto& e : range) {
            elemConvRef_.get().write(writer, e);
        }
        writer.endArray();
    }

    Container read(JsonParser& parser) const {
        Container out{};
        parser.startArray();
        while (!parser.nextIsEndArray()) {
            auto elem = elemConvRef_.get().read(parser);
            if constexpr (requires(Container& c, Element&& v) { c.push_back(std::declval<Element>()); }) {
                out.push_back(std::move(elem));
            }
            else if constexpr (requires(Container& c, Element&& v) { c.insert(std::declval<Element>()); }) {
                out.insert(std::move(elem));
            }
            else {
                static_assert(AlwaysFalse<Container>, "ContainerConverter: container must support push_back or insert");
            }
        }
        parser.endArray();
        return out;
    }

private:
    std::reference_wrapper<const ElementConverter> elemConvRef_{};
};

// ******************************************************************************** enum用

// JsonEnumMapのように、enum <-> 文字列名の双方向マップを提供する型のconcept。
export template <typename Map>
concept IsJsonEnumMap
    = requires { typename Map::Enum; }
    && std::is_enum_v<typename Map::Enum>
    && requires(const Map& m, std::string_view s, typename Map::Enum v) {
        { m.fromName(s) } -> std::same_as<std::optional<typename Map::Enum>>;
        { m.toName(v) } -> std::same_as<std::optional<std::string_view>>;
    };

/// EnumEntry holds a mapping from enum value to string name
export template <typename EnumType>
struct EnumEntry {
    EnumType value;   ///< Enum値。
    const char* name; ///< 対応する文字列名。
};

/// @brief EnumEntry を利用して enum <-> name の双方向マップを持つ再利用可能な型。
/// @tparam EnumType enum 型
/// @tparam N エントリ数（静的）
export template <typename EnumType, std::size_t N>
struct JsonEnumMap {
    using Enum = EnumType;

    /// @brief コンストラクタ（配列から構築）
    /// @param entries EnumEntry の配列
    constexpr explicit JsonEnumMap(const EnumEntry<Enum> (&entries)[N]) {
        std::pair<std::string_view, Enum> nv[N];
        for (std::size_t i = 0; i < N; ++i) {
            nv[i] = { entries[i].name, entries[i].value };
        }
        nameToValue_ = collection::SortedHashArrayMap<std::string_view, Enum, N>(nv);

        std::pair<Enum, std::string_view> vn[N];
        for (std::size_t i = 0; i < N; ++i) {
            vn[i] = { entries[i].value, entries[i].name };
        }
        valueToName_ = collection::SortedHashArrayMap<Enum, std::string_view, N>(vn);
    }

    /// @brief 文字列から enum を得る。見つからない場合は nullopt。
    constexpr std::optional<Enum> fromName(std::string_view name) const {
        if (auto p = nameToValue_.findValue(name)) return *p;
        return std::nullopt;
    }

    /// @brief enum から文字列名を得る。見つからない場合は nullopt。
    constexpr std::optional<std::string_view> toName(Enum v) const {
        if (auto p = valueToName_.findValue(v)) return *p;
        return std::nullopt;
    }

private:
    collection::SortedHashArrayMap<std::string_view, Enum, N> nameToValue_{}; ///< 名前からenum値へのマップ。
    collection::SortedHashArrayMap<Enum, std::string_view, N> valueToName_{}; ///< enum値から名前へのマップ。
};

/// @brief 列挙型用のコンバータ（外部の EnumJsonMap を参照する）
export template <typename MapType>
    requires IsJsonEnumMap<MapType>
struct EnumConverter {
    using Enum = typename MapType::Enum;
    using Value = Enum;
    constexpr explicit EnumConverter(MapType map) : map_(std::move(map)) {}
    void write(JsonWriter& writer, const Enum& value) const {
        if (auto name = map_.toName(value)) { writer.writeObject(*name); return; }
        throw std::runtime_error("Failed to convert enum to string");
    }
    Enum read(JsonParser& parser) const {
        std::string jsonValue;
        parser.readTo(jsonValue);
        if (auto v = map_.fromName(jsonValue)) return *v;
        throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
    }
private:
    MapType map_{};
};

// --- 補助ファクトリ (JsonField 用 - 名前衝突を避けるためリネーム)



// Element converter selection trait for containers
export template <typename Elem>
struct ElementConverterType {
    static_assert(AlwaysFalse<Elem>, "No element converter available for this element type; provide an explicit converter or specialize ElementConverterType");
};

export template <typename Elem>
requires (IsFundamentalValue<Elem> || std::same_as<Elem, std::string>)
struct ElementConverterType<Elem> { using type = FundamentalConverter<Elem>; };

export template <typename Elem>
requires HasJsonFields<Elem>
struct ElementConverterType<Elem> { using type = JsonFieldsConverter<Elem>; };

export template <typename Elem>
requires (HasReadJson<Elem> && HasWriteJson<Elem>)
struct ElementConverterType<Elem> { using type = WriteReadJsonConverter<Elem>; };

export template <typename Elem>
requires IsUniquePtr<Elem>
struct ElementConverterType<Elem> { using type = UniquePtrConverter<Elem>; };

export template <typename Elem>
requires IsStdVariant<Elem>
struct ElementConverterType<Elem> { using type = VariantConverter<Elem>; };

export template <typename Elem>
requires IsSmartOrRawPointer<Elem>
struct ElementConverterType<Elem> { static_assert(AlwaysFalse<Elem>, "Container element is pointer/polymorphic; use makeJsonPolymorphicArrayField or provide explicit element converter"); };

// Helper concept to detect available ElementConverter
export template <typename Elem>
concept HasElementConverter = requires { typename ElementConverterType<Elem>::type; };

// Helper: construct a converter for a given MemberPtrType's value type
export template <typename MemberPtrType>
constexpr auto makeConverter(MemberPtrType /*memberPtr*/, const char* /*keyName*/) {
    using ValueT = MemberPointerValueType<MemberPtrType>;

    if constexpr (IsFundamentalValue<ValueT> || std::same_as<ValueT, std::string>) {
        static const FundamentalConverter<ValueT> conv{};
        return std::cref(conv);
    }
    else if constexpr (HasJsonFields<ValueT>) {
        static const JsonFieldsConverter<ValueT> conv{};
        return std::cref(conv);
    }
    else if constexpr (HasReadJson<ValueT> && HasWriteJson<ValueT>) {
        static const WriteReadJsonConverter<ValueT> conv{};
        return std::cref(conv);
    }
    else if constexpr (IsUniquePtr<ValueT>) {
        static const UniquePtrConverter<ValueT> conv{};
        return std::cref(conv);
    }
    else if constexpr (IsStdVariant<ValueT>) {
        static const VariantConverter<ValueT> conv{};
        return std::cref(conv);
    }
    else if constexpr (IsContainer<ValueT>) {
        using Container = ValueT;
        using Elem = std::remove_cvref_t<std::ranges::range_value_t<Container>>;

        if constexpr (HasElementConverter<Elem>) {
            using ElemConv = typename ElementConverterType<Elem>::type;
            static const ElemConv econv{};
            static const ContainerConverter<Container, ElemConv> conv(econv);
            return std::cref(conv);
        }
        else if constexpr (IsSmartOrRawPointer<Elem>) {
            static_assert(AlwaysFalse<MemberPtrType>, "makeConverter: container element is pointer/polymorphic; use makeJsonPolymorphicArrayField or provide explicit element converter");
        }
        else {
            static_assert(AlwaysFalse<MemberPtrType>, "makeConverter: unsupported container element type; provide explicit element converter");
        }
    }
    else {
        static_assert(AlwaysFalse<MemberPtrType>, "makeConverter: unsupported field value type; provide explicit converter or use polymorphic helpers");
    }
}

// Single makeJsonField that selects an appropriate converter via helper
export template <typename MemberPtrType>
constexpr auto makeJsonField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    // Obtain a reference to an appropriate converter instance for this MemberPtrType
    auto convRef = makeConverter<MemberPtrType>(memberPtr, keyName);
    using ConvT = std::remove_cvref_t<decltype(convRef.get())>;
    return JsonField<MemberPtrType, ConvT>(memberPtr, keyName, convRef, req);
}




// ******************************************************************************** enum用



// end element converter selection (moved earlier)

export template <typename MemberPtrType, typename MapType>
constexpr auto makeJsonEnumField(MemberPtrType memberPtr, const char* keyName,
    const MapType& map, bool req = false) requires IsJsonEnumMap<MapType> && std::same_as<typename MapType::Enum, MemberPointerValueType<MemberPtrType>> {
    static const EnumConverter<MapType> conv(map);
    return JsonField<MemberPtrType, EnumConverter<MapType>>(memberPtr, keyName, std::cref(conv), req);
}

// Create a JsonField using an externally-managed EnumConverter (converter must outlive field)
export template <typename MemberPtrType, typename MapType>
constexpr auto makeJsonEnumFieldConverter(MemberPtrType memberPtr, const char* keyName,
    const EnumConverter<MapType>& conv, bool req = false) requires IsJsonEnumMap<MapType> && std::same_as<typename MapType::Enum, MemberPointerValueType<MemberPtrType>> {
    return JsonField<MemberPtrType, EnumConverter<MapType>>(memberPtr, keyName, std::cref(conv), req);
}



// ******************************************************************************** コンテナ用
export template <typename Enum, std::size_t N>
constexpr auto makeJsonEnumMap(const EnumEntry<Enum> (&entries)[N]) {
    return JsonEnumMap<Enum, N>(entries);
}



// ******************************************************************************** コンテナ用

/// @brief 配列形式のJSONを読み書きする汎用フィールド。
/// @tparam MemberPtrType コンテナ型のメンバー変数へのポインタ。
/// @details push_back または insert を持つコンテナに対応する。
// JsonContainerField has been removed. Use makeJsonField(...) which selects a ContainerConverter
// for container types. For convenience, provide a helper that forwards to makeJsonField.
export template <typename MemberPtrType>
constexpr auto makeJsonContainerField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    return makeJsonField(memberPtr, keyName, req);
}



// ******************************************************************************** トークン種別毎の分岐用

/// @brief JsonTokenTypeの総数。
/// @note この値はJsonTokenType enumの要素数と一致している必要がある。
export inline constexpr std::size_t JsonTokenTypeCount = 12;

/// @brief JSONからの読み取り用コンバータ型。
/// @tparam ValueType 変換対象の値型。
/// @note 配列添え字がJsonTokenTypeに対応する。
export template <typename ValueType>
using FromJsonEntry = std::function<ValueType(JsonParser&)>;

/// @brief トークン種別に応じた分岐コンバータ（JsonTokenDispatchField と同等）
export template <typename ValueType>
struct TokenDispatchConverter {
    using Value = ValueType;
    using ToConverter = std::function<void(JsonWriter&, const ValueType&)>;

    template <std::size_t FromN>
    explicit TokenDispatchConverter(const std::array<FromJsonEntry<ValueType>, FromN>& fromEntries,
        ToConverter toConverter = defaultToConverter())
        : toConverter_(std::move(toConverter)) {
        static_assert(FromN <= JsonTokenTypeCount);
        for (std::size_t i = 0; i < JsonTokenTypeCount; ++i) {
            fromEntries_[i] = [](JsonParser&) -> ValueType {
                throw std::runtime_error("No converter found for token type");
            };
        }
        for (std::size_t i = 0; i < FromN; ++i) {
            if (fromEntries[i]) fromEntries_[i] = fromEntries[i];
        }
    }

    ValueType read(JsonParser& parser) const {
        std::size_t index = static_cast<std::size_t>(parser.nextTokenType());
        return fromEntries_[index](parser);
    }

    void write(JsonWriter& writer, const ValueType& value) const {
        toConverter_(writer, value);
    }

    void setToConverter(ToConverter toConverter) { toConverter_ = std::move(toConverter); }

    static ToConverter defaultToConverter() {
        return [](JsonWriter& writer, const ValueType& value) { writer.writeObject(value); };
    }

private:
    std::array<std::function<ValueType(JsonParser&)>, JsonTokenTypeCount> fromEntries_{};
    ToConverter toConverter_{};
};
// --- TokenDispatchConverter（JsonTokenTypeCount の後に定義する）

/// @brief 補助ファクトリ（TokenDispatchはFromJsonEntryが必要なのでここに置く）
// Create a JsonField using an externally-managed TokenDispatchConverter (converter must outlive field)
export template <typename MemberPtrType, typename ValueType>
constexpr auto makeJsonTokenDispatchField(MemberPtrType memberPtr, const char* keyName,
    const TokenDispatchConverter<ValueType>& conv, bool req = false) requires std::same_as<ValueType, MemberPointerValueType<MemberPtrType>> {
    return JsonField<MemberPtrType, TokenDispatchConverter<ValueType>>(memberPtr, keyName, std::cref(conv), req);
}

// Convenience overload: construct a TokenDispatch JsonField from an array of FromJsonEntry
export template <typename MemberPtrType, typename ValueType, std::size_t FromN>
constexpr auto makeJsonTokenDispatchField(MemberPtrType memberPtr, const char* keyName,
    const std::array<FromJsonEntry<ValueType>, FromN>& fromEntries,
    typename TokenDispatchConverter<ValueType>::ToConverter toConverter = TokenDispatchConverter<ValueType>::defaultToConverter(),
    bool req = false) requires std::same_as<ValueType, MemberPointerValueType<MemberPtrType>> {
    static_assert(FromN <= JsonTokenTypeCount);
    // Note: conv references static data built from 'fromEntries', must outlive returned JsonField
    static const TokenDispatchConverter<ValueType> conv(fromEntries, toConverter);
    return JsonField<MemberPtrType, TokenDispatchConverter<ValueType>>(memberPtr, keyName, std::cref(conv), req);
}

// JsonTokenDispatchField has been removed. Use the factory helpers
// makeJsonTokenDispatchField(...) which construct a suitable TokenDispatchConverter
// and return a JsonField referencing it (static lifetime for convenience).

}  // namespace rai::json
