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

import rai.collection.sorted_hash_array_map;

namespace rai::json {

// ******************************************************************************** メタプログラミング、concept

/// @brief メンバーポインタの特性を抽出するメタ関数。
/// @tparam T メンバーポインタ型。
template <typename T>
struct MemberPointerTraits;

template <typename Owner, typename Value>
struct MemberPointerTraits<Value Owner::*> {
    using OwnerType = Owner;
    using ValueType = Value;
};

// 型エイリアス: メンバーポインタから ValueType を簡単に取り出す。
export template <typename MemberPtrType>
using MemberPointerValueType = typename MemberPointerTraits<MemberPtrType>::ValueType;

// JsonEnumMapのように、enum <-> 文字列名の双方向マップを提供する型のconcept。
export template <typename Converter, typename Value>
concept IsJsonConverter = std::is_class_v<Converter>
    && requires { typename Converter::Value; }
    && std::is_same_v<typename Converter::Value, Value>
    && requires(const Converter& converter, JsonWriter& writer, const Value& value) {
        converter.write(writer, value);
    }
    && requires(const Converter& converter, JsonParser& parser) {
        { converter.read(parser) } -> std::same_as<Value>;
    };


// ******************************************************************************** フィールド

/// @brief メンバー変数とJSON項目を対応付ける。
export template <typename MemberPtrType, typename Converter>
    requires std::is_member_object_pointer_v<MemberPtrType>
struct JsonField {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using OwnerType = typename Traits::OwnerType;
    using ValueType = typename Traits::ValueType;
    static_assert(IsJsonConverter<Converter, MemberPointerValueType<MemberPtrType>>,
        "Converter must satisfy IsJsonConverter for the member value type");

    // 参照で構築: コンバータは外部で所有/管理され、フィールドより長く存続する必要があります
    constexpr explicit JsonField(MemberPtrType memberPtr, const char* keyName,
        std::reference_wrapper<const Converter> conv, bool req = false)
        : member(memberPtr), converterRef(conv), key(keyName), required(req) {}

    // 便宜上のオーバーロード: const Converter& を受け取り参照を保存します（呼び出し側で寿命を保証してください）
    constexpr explicit JsonField(MemberPtrType memberPtr, const char* keyName,
        const Converter& conv, bool req = false)
        : member(memberPtr), converterRef(std::cref(conv)), key(keyName), required(req) {}

    void write(JsonWriter& writer, const ValueType& value) const {
        converterRef.get().write(writer, value);
    }

    ValueType read(JsonParser& parser) const {
        return converterRef.get().read(parser);
    }

    MemberPtrType member{};                               ///< メンバポインタ
    std::reference_wrapper<const Converter> converterRef; ///< 値変換器への参照（必ず有効であること）
    const char* key{};                                    ///< JSONキー名
    bool required{false};                                 ///< 必須か
};


// ******************************************************************************** 基本型用変換方法

/// @brief 基本型等を扱うコンバータ（value_io に委譲）
export template <typename T>
    requires (IsFundamentalValue<T> || std::same_as<T, std::string>)
struct FundamentalConverter {
    using Value = T;
    void write(JsonWriter& writer, const T& value) const { writer.writeObject(value); }
    T read(JsonParser& parser) const {
        T out{};
        parser.readTo(out);
        return out;
    }
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
    T read(JsonParser& parser) const {
        T out{};
        out.readJson(parser);
        return out;
    }
};


// ******************************************************************************** variant用変換方法

// Fallback: exclude complex types and provide clear diagnostics
export template <typename T>
constexpr auto& getConverter() {
    if constexpr (IsFundamentalValue<T> || std::same_as<T, std::string>) {
        static const FundamentalConverter<T> inst{};
        return inst;
    }
    else if constexpr (HasJsonFields<T>) {
        static const JsonFieldsConverter<T> inst{};
        return inst;
    }
    else if constexpr (HasReadJson<T> && HasWriteJson<T>) {
        static const WriteReadJsonConverter<T> inst{};
        return inst;
    }
    else if constexpr (IsUniquePtr<T>) {
        static_assert(false,
            "getConverter: unique_ptr types are excluded; use makeJsonUniquePtrField or provide an explicit UniquePtrConverter");
    }
    else if constexpr (IsStdVariant<T>) {
        static_assert(false,
            "getConverter: variant types are excluded; use makeJsonVariantField or provide an explicit VariantConverter");
    }
    else if constexpr (IsContainer<T>) {
        static_assert(false,
            "getConverter: container types are excluded; use makeJsonContainerField or provide an explicit ContainerConverter");
    }
    else {
        static_assert(false,
            "getConverter: unsupported type");
    }
}

export template <typename T>
    requires IsStdVariant<T>
struct VariantConverter;

// Variant
// VariantConverter: std::variant のトークン派生 (token-dispatch) と read/write を実装します
export template <typename T>
    requires IsStdVariant<T>
struct VariantConverter {
    using Value = T;

    void write(JsonWriter& writer, const T& v) const {
        std::visit([&writer](const auto& inner) {
            using Inner = std::remove_cvref_t<decltype(inner)>;
            static const auto& conv = getConverter<Inner>();
            conv.write(writer, inner);
        }, v);
    }

    T read(JsonParser& parser) const {
        using VariantType = T;
        auto tokenType = parser.nextTokenType();
        VariantType out{};
        bool matched = false;

        auto helper = [&](auto idx) {
            if (matched) {
                return;
            }
            constexpr std::size_t I = decltype(idx)::value;
            using Alternative = std::variant_alternative_t<I, VariantType>;
            using Decayed = std::remove_cvref_t<Alternative>;
            bool ok = false;
            switch (tokenType) {
            case JsonTokenType::Null:
                ok = IsUniquePtr<Decayed>;
                break;
            case JsonTokenType::Bool:
                ok = std::is_same_v<Decayed, bool>;
                break;
            case JsonTokenType::Integer:
                ok = std::is_integral_v<Decayed> && !std::is_same_v<Decayed, bool>;
                break;
            case JsonTokenType::Number:
                ok = std::is_floating_point_v<Decayed>;
                break;
            case JsonTokenType::String:
                ok = std::is_same_v<Decayed, std::string>;
                break;
            case JsonTokenType::StartObject:
                ok = HasJsonFields<Decayed> || HasReadJson<Decayed> || IsUniquePtr<Decayed>;
                break;
            case JsonTokenType::StartArray:
                ok = IsStdVector<Decayed>;
                break;
            default:
                ok = false;
                break;
            }
            if (ok) {
                const auto& altConv = getConverter<Alternative>();
                out = VariantType(std::in_place_index<I>, altConv.read(parser));
                matched = true;
            }
        };

        auto dispatch = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (helper(std::integral_constant<std::size_t, Is>{}), ...);
        };
        dispatch(std::make_index_sequence<std::variant_size_v<VariantType>>{});

        if (!matched) {
            throw std::runtime_error("Failed to dispatch variant for current token");
        }
        return out;
    }
};


// ******************************************************************************** unique_ptr用変換方法

/// @brief unique_ptr 等のコンバータ
export template <typename T, typename TargetConverter>
    requires IsUniquePtr<T>
struct UniquePtrConverter {
    using Value = T;
    using Element = typename T::element_type;
    using ElemConvT = std::remove_cvref_t<TargetConverter>;

    // デフォルト要素コンバータへの参照を返すユーティリティ（静的寿命）
    static const ElemConvT& defaultTargetConverter() {
        static const ElemConvT& inst = getConverter<Element>();
        return inst;
    }

    // デフォルトコンストラクタはデフォルト要素コンバータへの参照を初期化子リストで設定する
    UniquePtrConverter()
        : targetConverter_(std::cref(defaultTargetConverter())) {}

    // 明示的に要素コンバータ参照を指定するオーバーロード
    constexpr explicit UniquePtrConverter(const ElemConvT& conv)
        : targetConverter_(std::cref(conv)) {}

    void write(JsonWriter& writer, const T& ptr) const {
        if (!ptr) {
            writer.null();
            return;
        }
        targetConverter_.get().write(writer, *ptr);
    }

    T read(JsonParser& parser) const {
        if (parser.nextIsNull()) {
            parser.skipValue();
            return nullptr;
        }
        auto elem = targetConverter_.get().read(parser);
        return std::make_unique<Element>(std::move(elem));
    }

private:
    std::reference_wrapper<const ElemConvT> targetConverter_;
};

/// @brief unique_ptr<T>のjson変換方法を返す。
export template <typename T>
    requires IsUniquePtr<T>
constexpr auto& getUniquePtrConverter() {
    using TargetConverter = decltype(getConverter<typename T::element_type>());
    static const UniquePtrConverter<T, TargetConverter> inst{};
    return inst;
}

export template <typename MemberPtrType>
constexpr auto makeJsonUniquePtrField(
    MemberPtrType memberPtr, const char* keyName, bool req = false)
    requires std::is_member_object_pointer_v<MemberPtrType>
        && IsUniquePtr<MemberPointerValueType<MemberPtrType>> {
    using Ptr = MemberPointerValueType<MemberPtrType>;
    auto& converter = getUniquePtrConverter<Ptr>();
    return JsonField<MemberPtrType, std::remove_cvref_t<decltype(converter)>>(
        memberPtr, keyName, std::cref(converter), req);
}

export template <typename MemberPtrType, typename PtrConv>
    requires std::is_member_object_pointer_v<MemberPtrType>
        && IsUniquePtr<MemberPointerValueType<MemberPtrType>>
constexpr auto makeJsonUniquePtrField(
    MemberPtrType memberPtr, const char* keyName, const PtrConv& conv, bool req = false) {
    return JsonField<MemberPtrType, std::remove_cvref_t<PtrConv>>(
        memberPtr, keyName, std::cref(conv), req);
}

// ******************************************************************************** コンテナ用変換方法

/// @brief コンテナ用コンバータ（要素コンバータ参照を持つ）。
export template <typename Container, typename ElementConverter>
    requires IsContainer<Container>
        && IsJsonConverter<ElementConverter,
            std::remove_cvref_t<std::ranges::range_value_t<Container>>>
struct ContainerConverter {
    using Value = Container;
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    using ElementConverterT = std::remove_cvref_t<ElementConverter>;
    static_assert(std::is_same_v<typename ElementConverterT::Value, Element>,
        "ElementConverter::Value must match container element type");

    static const ElementConverterT& defaultElementConverter() {
        static const ElementConverterT inst{};
        return inst;
    }

    constexpr ContainerConverter()
        : elemConvRef_(std::cref(defaultElementConverter())) {}

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
            if constexpr (requires(Container& c, Element&& v) {
                    c.push_back(std::declval<Element>());
                }) {
                out.push_back(std::move(elem));
            }
            else if constexpr (requires(Container& c, Element&& v) {
                    c.insert(std::declval<Element>());
                }) {
                out.insert(std::move(elem));
            }
            else {
                static_assert(false,
                    "ContainerConverter: container must support push_back or insert");
            }
        }
        parser.endArray();
        return out;
    }

private:
    std::reference_wrapper<const ElementConverterT> elemConvRef_{};
};

// ******************************************************************************** enum用返還方法

// JsonEnumMapのように、enum <-> 文字列名の双方向マップを提供する型のconcept。
export template <typename Map>
concept IsJsonEnumMap
    = requires { typename Map::Enum; }
    && std::is_enum_v<typename Map::Enum>
    && requires(const Map& m, std::string_view s, typename Map::Enum v) {
        { m.fromName(s) } -> std::same_as<std::optional<typename Map::Enum>>;
        { m.toName(v) } -> std::same_as<std::optional<std::string_view>>;
    };

/// @brief EnumEntry は enum 値と文字列名の対応を保持します
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
        if (auto p = nameToValue_.findValue(name)) {
            return *p;
        }
        return std::nullopt;
    }

    /// @brief enum から文字列名を得る。見つからない場合は nullopt。
    constexpr std::optional<std::string_view> toName(Enum v) const {
        if (auto p = valueToName_.findValue(v)) {
            return *p;
        }
        return std::nullopt;
    }

private:
    ///! 名前からenum値へのマップ。
    collection::SortedHashArrayMap<std::string_view, Enum, N> nameToValue_{};
    ///! enum値から名前へのマップ。
    collection::SortedHashArrayMap<Enum, std::string_view, N> valueToName_{};
};

/// @brief 列挙型用のコンバータ
/// @tparam MapType JsonEnumMap型など
export template <typename MapType>
    requires IsJsonEnumMap<MapType>
struct EnumConverter {
    using Enum = typename MapType::Enum;
    using Value = Enum;
    constexpr explicit EnumConverter(const MapType& map)
        : map_(map) {}

    void write(JsonWriter& writer, const Enum& value) const {
        if (auto name = map_.toName(value)) {
            writer.writeObject(*name);
            return;
        }
        throw std::runtime_error("Failed to convert enum to string");
    }

    Enum read(JsonParser& parser) const {
        std::string jsonValue;
        parser.readTo(jsonValue);
        if (auto v = map_.fromName(jsonValue)) {
            return *v;
        }
        throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
    }
private:
    MapType map_{};
};

export template <typename Enum, std::size_t N>
constexpr auto makeJsonEnumMap(const EnumEntry<Enum> (&entries)[N]) {
    return JsonEnumMap<Enum, N>(entries);
}

export template <typename MemberPtrType, typename MapType>
constexpr auto makeJsonEnumField(MemberPtrType memberPtr, const char* keyName,
    const MapType& map, bool req = false)
    requires IsJsonEnumMap<MapType>
        && std::same_as<typename MapType::Enum, MemberPointerValueType<MemberPtrType>> {
    static const EnumConverter<MapType> conv(map);
    return JsonField<MemberPtrType, EnumConverter<MapType>>(
        memberPtr, keyName, std::cref(conv), req);
}

// 外部で管理される EnumConverter を使って JsonField を作成します（コンバータはフィールドより長く存続する必要があります）
export template <typename MemberPtrType, typename MapType>
constexpr auto makeJsonEnumFieldConverter(MemberPtrType memberPtr, const char* keyName,
    const EnumConverter<MapType>& conv, bool req = false)
    requires IsJsonEnumMap<MapType>
        && std::same_as<typename MapType::Enum, MemberPointerValueType<MemberPtrType>> {
    return JsonField<MemberPtrType, EnumConverter<MapType>>(
        memberPtr, keyName, std::cref(conv), req);
}


// ******************************************************************************** 変換方法取得
// --- 補助ファクトリ (JsonField 用 - 名前衝突を避けるためリネーム)

// ヘルパ: 与えられた MemberPtrType の値型に対するコンバータを構築します
// 注意: このファクトリは基本的な値型や HasJsonFields/HasReadJson の場合のみ自動的にコンバータを提供します。
//       unique_ptr、variant、コンテナなど複雑なケースはここでは対象外とし、明示的なコンバータを要求します。
export template <typename MemberPtrType>
constexpr auto makeJsonField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    using ValueT = MemberPointerValueType<MemberPtrType>;
    const auto& conv = getConverter<ValueT>();
    using ConvT = std::remove_cvref_t<decltype(conv)>;
    return JsonField<MemberPtrType, ConvT>(memberPtr, keyName, std::cref(conv), req);
}  



// ******************************************************************************** コンテナ用

/// @brief 配列形式のJSONを読み書きする汎用フィールド。
/// @tparam MemberPtrType コンテナ型のメンバー変数へのポインタ。
/// @details push_back または insert を持つコンテナに対応する。
// JsonContainerField helper: construct a ContainerConverter for the container element if available
export template <typename MemberPtrType>
constexpr auto makeJsonContainerField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    using Container = MemberPointerValueType<MemberPtrType>;
    static_assert(IsContainer<Container>, "makeJsonContainerField: member is not a container type");
    using Elem = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    const auto& elemConvInstance = getConverter<Elem>();
    using ElemConv = std::remove_cvref_t<decltype(elemConvInstance)>;
    static const ContainerConverter<Container, ElemConv> conv(elemConvInstance);
    return JsonField<MemberPtrType, std::remove_cvref_t<decltype(conv)>>(memberPtr, keyName, std::cref(conv), req);
} 

// Overload: accept an explicit ContainerConverter instance to avoid copying element converters.
export template <typename MemberPtrType, typename Container, typename ElemConv>
    requires std::is_member_object_pointer_v<MemberPtrType>
        && std::same_as<MemberPointerValueType<MemberPtrType>, Container>
        && IsContainer<Container>
        && IsJsonConverter<ElemConv, std::remove_cvref_t<std::ranges::range_value_t<Container>>>
constexpr auto makeJsonContainerField(MemberPtrType memberPtr, const char* keyName,
    const ContainerConverter<Container, ElemConv>& conv, bool req = false) {
    return JsonField<MemberPtrType, std::remove_cvref_t<ContainerConverter<Container, ElemConv>>>(
        memberPtr, keyName, std::cref(conv), req);
}

// Helpers for variant members (default uses VariantConverter)
export template <typename MemberPtrType>
constexpr auto makeJsonVariantField(MemberPtrType memberPtr, const char* keyName, bool req = false)
    requires std::is_member_object_pointer_v<MemberPtrType> && IsStdVariant<MemberPointerValueType<MemberPtrType>> {
    using Var = MemberPointerValueType<MemberPtrType>;
    static const VariantConverter<Var> conv{};
    return JsonField<MemberPtrType, VariantConverter<Var>>(memberPtr, keyName, std::cref(conv), req);
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
        static_assert(FromN <= 12);
        for (std::size_t i = 0; i < 12; ++i) {
            fromEntries_[i] = [](JsonParser&) -> ValueType {
                throw std::runtime_error("No converter found for token type");
            };
        }
        for (std::size_t i = 0; i < FromN; ++i) {
            if (fromEntries[i]) {
                fromEntries_[i] = fromEntries[i];
            }
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
    std::array<std::function<ValueType(JsonParser&)>, 12> fromEntries_{};
    ToConverter toConverter_{};
};
// --- TokenDispatchConverter（JsonTokenTypeCount の後に定義する）

/// @brief 補助ファクトリ（TokenDispatchはFromJsonEntryが必要なのでここに置く）
// 外部で管理される TokenDispatchConverter を使って JsonField を作成します（コンバータはフィールドより長く存続する必要があります）
export template <typename MemberPtrType, typename ValueType>
constexpr auto makeJsonTokenDispatchField(MemberPtrType memberPtr, const char* keyName,
    const TokenDispatchConverter<ValueType>& conv, bool req = false) requires std::same_as<ValueType, MemberPointerValueType<MemberPtrType>> {
    return JsonField<MemberPtrType, TokenDispatchConverter<ValueType>>(memberPtr, keyName, std::cref(conv), req);
}

// 便宜上のオーバーロード: FromJsonEntry の配列から TokenDispatch 用の JsonField を構築します
export template <typename MemberPtrType, typename ValueType, std::size_t FromN>
constexpr auto makeJsonTokenDispatchField(MemberPtrType memberPtr, const char* keyName,
    const std::array<FromJsonEntry<ValueType>, FromN>& fromEntries,
    typename TokenDispatchConverter<ValueType>::ToConverter toConverter = TokenDispatchConverter<ValueType>::defaultToConverter(),
    bool req = false) requires std::same_as<ValueType, MemberPointerValueType<MemberPtrType>> {
    static_assert(FromN <= 12);
    // 補足: conv は 'fromEntries' から構築した静的データを参照するため、返される JsonField より長い寿命を持つ必要があります
    static const TokenDispatchConverter<ValueType> conv(fromEntries, toConverter);
    return JsonField<MemberPtrType, TokenDispatchConverter<ValueType>>(memberPtr, keyName, std::cref(conv), req);
}

// JsonTokenDispatchField は削除されました。代わりにファクトリヘルパ
// makeJsonTokenDispatchField(...) を使用してください。これは適切な TokenDispatchConverter
// を構築し、それを参照する JsonField を返します（利便性のために静的な寿命を利用）。

}  // namespace rai::json
