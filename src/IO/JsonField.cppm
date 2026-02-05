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
#include <span>

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

/// @brief メンバーポインタ型から対応する値型を取り出す型エイリアス。
export template <typename MemberPtr>
using MemberPointerValueType = typename MemberPointerTraits<MemberPtr>::ValueType;

/// @brief SingleValueFieldOmitBehavior を利用できるか判定するconcept。
/// @tparam ValueType 対象型
template <typename ValueType>
concept IsSingleValueBehaviorAllowed = std::is_copy_constructible_v<ValueType>
    && std::is_copy_assignable_v<ValueType>
    && std::equality_comparable<ValueType>
    && (!IsContainer<ValueType>
        || (std::is_copy_constructible_v<std::remove_cvref_t<std::ranges::range_value_t<ValueType>>>
            && std::is_copy_assignable_v<std::remove_cvref_t<std::ranges::range_value_t<ValueType>>>))
    && (!IsUniquePtr<ValueType>);

/// @brief JSONへの書き出しと読み込みを行うコンバータに要求される条件を定義する concept。
/// @tparam Converter コンバータ型
/// @tparam Value コンバータが扱う値の型
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

/// @brief JsonFieldの省略時挙動を満たす型の concept。
/// @tparam Behavior 挙動型
/// @tparam Value 値型
export template <typename Behavior, typename Value>
concept IsJsonFieldOmittedBehavior = requires(
    const Behavior& behavior,
    const Value& value,
    Value& outValue,
    std::string_view key) {
    { behavior.shouldSkipWrite(value) } -> std::same_as<bool>;
    { behavior.applyMissing(outValue, key) } -> std::same_as<void>;
};

/// @brief 読み込み時省略では既定値を設定し、書き出し時既定値と一致するならスキップする省略時挙動。
/// @tparam ValueType 値型
export template <typename ValueType>
struct SingleValueFieldOmitBehavior {
    static_assert(IsSingleValueBehaviorAllowed<ValueType>,
        "SingleValueFieldOmitBehavior requires copyable ValueType");
    static_assert(std::equality_comparable<ValueType>,
        "SingleValueFieldOmitBehavior requires equality comparable ValueType");
    ValueType defaultValue{};  ///< 省略判定と欠落時代入に使う値

    /// @brief 値が省略条件に一致するかを返す。
    /// @param value チェックする値
    /// @return 省略すべきなら true
    bool shouldSkipWrite(const ValueType& value) const {
        return value == defaultValue;
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param outValue 欠落時に代入する対象
    /// @param key 対象キー名
    void applyMissing(ValueType& outValue, std::string_view key) const {
        (void)key;
        outValue = defaultValue;
    }
};

/// @brief 省略時挙動（既定値も省略条件も持たない）。
/// @tparam ValueType 値型
export template <typename ValueType>
struct NoDefaultFieldOmitBehavior {
    /// @brief 値が省略条件に一致するかを返す。
    /// @param value チェックする値
    /// @return 省略すべきなら true
    bool shouldSkipWrite(const ValueType& value) const {
        (void)value;
        return false;
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param outValue 欠落時に代入する対象
    /// @param key 対象キー名
    void applyMissing(ValueType& outValue, std::string_view key) const {
        (void)outValue;
        (void)key;
    }
};

/// @brief 読み込み時省略では例外を送出し、常時書き出しす省略時挙動。
/// @tparam ValueType 値型
export template <typename ValueType>
struct RequiredFieldOmitBehavior {
    /// @brief 値が省略条件に一致するかを返す。
    /// @param value チェックする値
    /// @return 省略すべきなら true
    bool shouldSkipWrite(const ValueType& value) const {
        (void)value;
        return false;
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param outValue 欠落時に代入する対象
    /// @param key 対象キー名
    void applyMissing(ValueType& outValue, std::string_view key) const {
        (void)outValue;
        throw std::runtime_error(
            std::string("JsonParser: missing required key '") +
            std::string(key) + "'");
    }
};

// ******************************************************************************** フィールド

/// @brief メンバー変数とJSON項目を対応付ける。
export template <typename MemberPtr, typename Converter, typename OmittedBehavior>
struct JsonField {
    static_assert(std::is_member_object_pointer_v<MemberPtr>,
        "JsonField requires MemberPtr to be a member object pointer");
    using Traits = MemberPointerTraits<MemberPtr>; 
    using OwnerType = typename Traits::OwnerType;
    using ValueType = typename Traits::ValueType;
    static_assert(IsJsonConverter<Converter, MemberPointerValueType<MemberPtr>>,
        "Converter must satisfy IsJsonConverter for the member value type");
    static_assert(IsJsonFieldOmittedBehavior<OmittedBehavior, ValueType>,
        "OmittedBehavior must satisfy IsJsonFieldOmittedBehavior for the member value type");

    /// @brief コンストラクタ（省略時挙動を明示的に指定する版）。
    /// @param memberPtr メンバポインタ
    /// @param keyName JSONキー名
    /// @param conv コンバータへの参照（呼び出し側で寿命を保証）
    /// @param behavior 省略時挙動
    constexpr explicit JsonField(MemberPtr memberPtr, const char* keyName,
        std::reference_wrapper<const Converter> conv, OmittedBehavior behavior)
        : member(memberPtr), converter_(conv), key(keyName),
          omittedBehavior_(std::move(behavior)) {}

    /// @brief JSON から値を読み取る。
    /// @param parser 読み取り元の JsonParser
    /// @return 変換された値
    ValueType read(JsonParser& parser) const {
        return converter_.get().read(parser);
    }

    /// @brief JSON項目（キーと値）を書き出す。
    /// @param writer 書き込み先の JsonWriter
    /// @param value 書き込む値
    void writeKeyValue(JsonWriter& writer, const ValueType& value) const {
        if (omittedBehavior_.shouldSkipWrite(value)) {
            return;
        }
        writer.key(key);
        converter_.get().write(writer, value);
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param outValue 欠落時に代入する対象
    void applyMissing(ValueType& outValue) const {
        omittedBehavior_.applyMissing(outValue, key);
    }

    MemberPtr member{};                               ///< メンバポインタ
    const char* key{};                                    ///< JSONキー名
private:
    std::reference_wrapper<const Converter> converter_;  ///< 値変換器への参照（必ず有効であること）
    OmittedBehavior omittedBehavior_{};                  ///< 省略時挙動
};

/// @brief JsonField の型推論ガイド（reference_wrapper + 省略時挙動）。
/// @tparam MemberPtr メンバポインタ型
/// @tparam Converter コンバータ型
/// @tparam OmittedBehavior 省略時挙動型
export template <typename MemberPtr, typename Converter, typename OmittedBehavior>
JsonField(MemberPtr, const char*, std::reference_wrapper<const Converter>, OmittedBehavior)
    -> JsonField<MemberPtr, Converter, OmittedBehavior>;


// ******************************************************************************** 基本型用変換方法

/// @brief 基本型等を扱うコンバータ（value_io に委譲）
export template <typename T>
struct FundamentalConverter {
    static_assert(IsFundamentalValue<T> || std::same_as<T, std::string>,
        "FundamentalConverter requires T to be a fundamental JSON value or std::string");
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
struct JsonFieldsConverter {
    static_assert(HasJsonFields<T> && std::default_initializable<T>,
        "JsonFieldsConverter requires T to have jsonFields() and be default-initializable");
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
        fields.readObject(parser, &obj);
        parser.endObject();
        return obj;
    }
};

/// @brief writeJson/readJson を持つ型のコンバータ
export template <typename T>
struct ReadWriteJsonConverter {
    static_assert(HasReadJson<T> && HasWriteJson<T> && std::default_initializable<T>,
        "ReadWriteJsonConverter requires T to have readJson/writeJson and be default-initializable");
    using Value = T; 
    void write(JsonWriter& writer, const T& obj) const {
        obj.writeJson(writer);
    }
    T read(JsonParser& parser) const {
        T out{};
        out.readJson(parser);
        return out;
    }
};

/// @brief 型 `T` に応じた既定のコンバータを返すユーティリティ。
/// @note 基本型、`HasJsonFields`、`HasReadJson`/`HasWriteJson` を持つ型を自動的に扱い、その他の複雑な型は明確な static_assert で除外します。
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
        static const ReadWriteJsonConverter<T> inst{};
        return inst;
    }
    else {
        static_assert(false,
            "getConverter: unsupported type");
    }
}

/// @brief 値変換方法と省略時挙動を指定しJsonFieldを作って返す。
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
/// @param converter 値型に対応するコンバータ
/// @param omitBehavior 省略時挙動
export template <typename MemberPtr, typename Converter, typename OmitBehavior>
constexpr auto getField(MemberPtr memberPtr, const char* keyName,
    const Converter& converter, const OmitBehavior& omitBehavior) {
    using ConverterBody = std::remove_cvref_t<Converter>;
    return JsonField<MemberPtr, ConverterBody, OmitBehavior>(
        memberPtr, keyName, std::cref(converter), omitBehavior);
}

/// @brief 項目必須のJsonFieldを作って返す。
///        与えられた MemberPtr の値型（以下）に対するコンバータを使用する。
///        基本型、HasJsonFields、HasReadJson/HasWriteJson
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
export template <typename MemberPtr>
constexpr auto getRequiredField(MemberPtr memberPtr, const char* keyName) {
    using Value = MemberPointerValueType<MemberPtr>;
    const auto& converter = getConverter<Value>();
    return getField(memberPtr, keyName, converter,
        RequiredFieldOmitBehavior<Value>{});
}

/// @brief 項目必須のJsonFieldを作って返す（コンバータ指定版）。
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
/// @param converter 値型に対応するコンバータ
export template <typename MemberPtr, typename Converter>
constexpr auto getRequiredField(MemberPtr memberPtr, const char* keyName,
    const Converter& converter) {
    return getField(memberPtr, keyName, converter,
        RequiredFieldOmitBehavior<MemberPointerValueType<MemberPtr>>{});
}

/// @brief 読み込み時省略では既定値を代入し、書き込み時は既定値と等しい場合に省略するJsonFieldを返す。
///        与えられた MemberPtr の値型（以下）に対するコンバータを使用する。
///        基本型、HasJsonFields、HasReadJson/HasWriteJson
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
/// @param defaultValue 欠落時に代入し、書き込み時の省略判定に使う値
export template <typename MemberPtr>
constexpr auto getDefaultOmittedField(MemberPtr memberPtr, const char* keyName,
    MemberPointerValueType<MemberPtr> defaultValue) {
    using Value = MemberPointerValueType<MemberPtr>;
    const auto& converter = getConverter<Value>();
    SingleValueFieldOmitBehavior<Value> behavior{ .defaultValue = std::move(defaultValue) };
    return getField(memberPtr, keyName, converter, behavior);
}

/// @brief 読み込み時省略では既定値を代入し、書き込み時は既定値と等しい場合に省略するJsonFieldを返す。
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
/// @param defaultValue 欠落時に代入し、書き込み時の省略判定に使う値
/// @param converter 値型に対応するコンバータ
export template <typename MemberPtr, typename Converter>
constexpr auto getDefaultOmittedField(MemberPtr memberPtr, const char* keyName,
    MemberPointerValueType<MemberPtr> defaultValue, const Converter& converter) {
    SingleValueFieldOmitBehavior<MemberPointerValueType<MemberPtr>> behavior
    { .defaultValue = std::move(defaultValue) };
    return getField(memberPtr, keyName, converter, behavior);
}

/// @brief 読み込み時省略では何も行わず、書き込み時のみ指定値と等しい場合に省略するJsonFieldを返す。
///        与えられた MemberPtr の値型（以下）に対するコンバータを使用する。
///        基本型、HasJsonFields、HasReadJson/HasWriteJson
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
/// @param skipValue 書き込み時の省略判定に使う値
export template <typename MemberPtr>
constexpr auto getInitialOmittedField(MemberPtr memberPtr, const char* keyName,
    const MemberPointerValueType<MemberPtr>& skipValue) {
    using Value = MemberPointerValueType<MemberPtr>;
    const auto& converter = getConverter<Value>();
    SingleValueFieldOmitBehavior<Value> behavior
    { .defaultValue = skipValue };
    return getField(memberPtr, keyName, converter, behavior);
}

/// @brief 読み込み時省略では何も行わず、書き込み時のみ指定値と等しい場合に省略するJsonFieldを返す。
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
/// @param skipValue 書き込み時の省略判定に使う値
/// @param converter 値型に対応するコンバータ
export template <typename MemberPtr, typename Converter>
constexpr auto getInitialOmittedField(MemberPtr memberPtr, const char* keyName,
    const MemberPointerValueType<MemberPtr>& skipValue, const Converter& converter) {
    SingleValueFieldOmitBehavior<MemberPointerValueType<MemberPtr>> behavior
    { .defaultValue = skipValue };
    return getField(memberPtr, keyName, converter, behavior);
}

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

    /// @brief std::span ベースのコンストラクタ（C配列やstd::arrayからの変換を受け取ります）
    constexpr explicit JsonEnumMap(std::span<const EnumEntry<Enum>> entries) {
        if (entries.size() != N) {
            throw std::runtime_error("JsonEnumMap(span): size must match template parameter N");
        }
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
struct EnumConverter {
    static_assert(IsJsonEnumMap<MapType>,
        "EnumConverter requires MapType to satisfy IsJsonEnumMap");
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

/// @brief C 配列から JsonEnumMap を構築する。
export template <typename Enum, std::size_t N>
constexpr auto makeJsonEnumMap(const EnumEntry<Enum> (&entries)[N]) {
    return JsonEnumMap<Enum, N>(std::span<const EnumEntry<Enum>, N>(entries));
}

/// @brief array から JsonEnumMap を構築する。
export template <typename Enum, std::size_t M>
constexpr auto makeJsonEnumMap(const std::array<EnumEntry<Enum>, M>& entries) {
    return JsonEnumMap<Enum, M>(std::span<const EnumEntry<Enum>, M>(entries.data(), M));
}

/// @brief spanから JsonEnumMap を構築する。
export template <typename Enum, std::size_t N>
constexpr auto makeJsonEnumMap(std::span<const EnumEntry<Enum>, N> entries) {
    return JsonEnumMap<Enum, N>(entries);
}

/// @brief 列挙型メンバに対する `JsonField` を作成する（`JsonEnumMap` を渡す版）。
export template <typename MemberPtr, typename MapType>
constexpr auto makeJsonEnumField(MemberPtr memberPtr, const char* keyName,
    const MapType& map) {
    static_assert(IsJsonEnumMap<MapType>,
        "makeJsonEnumField requires MapType to satisfy IsJsonEnumMap");
    static_assert(std::same_as<typename MapType::Enum, MemberPointerValueType<MemberPtr>>,
        "MapType::Enum must match the member's value type");
    static const EnumConverter<MapType> conv(map);
    using Value = MemberPointerValueType<MemberPtr>;
    using Behavior = RequiredFieldOmitBehavior<Value>;
    return JsonField<MemberPtr, EnumConverter<MapType>, Behavior>(
        memberPtr, keyName, std::cref(conv), Behavior{});
}

/// @brief 外部で管理される `EnumConverter` を用いて `JsonField` を作成する（コンバータはフィールドより長く存続する必要があります）。
export template <typename MemberPtr, typename MapType>
constexpr auto makeJsonEnumField(MemberPtr memberPtr, const char* keyName,
    const EnumConverter<MapType>& conv) {
    static_assert(IsJsonEnumMap<MapType>,
        "makeJsonEnumField requires MapType to satisfy IsJsonEnumMap");
    static_assert(std::same_as<typename MapType::Enum, MemberPointerValueType<MemberPtr>>,
        "MapType::Enum must match the member's value type");
    using Value = MemberPointerValueType<MemberPtr>;
    using Behavior = RequiredFieldOmitBehavior<Value>;
    return JsonField<MemberPtr, EnumConverter<MapType>, Behavior>(
        memberPtr, keyName, std::cref(conv), Behavior{});
}


// ******************************************************************************** コンテナ用変換方法

/// @brief コンテナ用コンバータ（要素コンバータ参照を持つ）。
export template <typename Container, typename ElementConverter>
struct ContainerConverter {
    static_assert(IsContainer<Container>,
        "ContainerConverter requires Container to be a container type");
    using Value = Container; 
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static_assert(IsJsonConverter<ElementConverter, Element>,
        "ElementConverter must satisfy IsJsonConverter for container element type");
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

/// @brief 配列形式のJSONを読み書きする汎用フィールド。
/// @tparam MemberPtr コンテナ型のメンバー変数へのポインタ。
/// @details push_back または insert を持つコンテナに対応する。
// JsonContainerField helper: construct a ContainerConverter for the container element if available
export template <typename MemberPtr>
constexpr auto makeJsonContainerField(MemberPtr memberPtr, const char* keyName) {
    using Container = MemberPointerValueType<MemberPtr>;
    static_assert(IsContainer<Container>,
        "makeJsonContainerField: member is not a container type");
    using Elem = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    const auto& elemConvInstance = getConverter<Elem>();
    using ElemConv = std::remove_cvref_t<decltype(elemConvInstance)>;
    static const ContainerConverter<Container, ElemConv> conv(elemConvInstance);
    using ConverterBody = std::remove_cvref_t<decltype(conv)>;
    using Behavior = RequiredFieldOmitBehavior<Container>;
    return JsonField<MemberPtr, ConverterBody, Behavior>(
        memberPtr, keyName, std::cref(conv), Behavior{});
} 

/// @brief 明示的な `ContainerConverter` を渡して `JsonField` を作成するオーバーロード。
export template <typename MemberPtr, typename Container, typename ElemConv>
constexpr auto makeJsonContainerField(MemberPtr memberPtr, const char* keyName,
    const ContainerConverter<Container, ElemConv>& conv) {
    static_assert(std::is_member_object_pointer_v<MemberPtr>,
        "makeJsonContainerField requires MemberPtr to be a member object pointer");
    static_assert(std::same_as<MemberPointerValueType<MemberPtr>, Container>,
        "Member pointer value must match Container");
    static_assert(IsContainer<Container>,
        "makeJsonContainerField requires Container to be a container type");
    static_assert(IsJsonConverter<ElemConv, std::remove_cvref_t<std::ranges::range_value_t<Container>>>,
        "ElemConv must be a JsonConverter for the container element type");
    using ConverterBody = std::remove_cvref_t<ContainerConverter<Container, ElemConv>>;
    using Behavior = RequiredFieldOmitBehavior<Container>;
    return JsonField<MemberPtr, ConverterBody, Behavior>(
        memberPtr, keyName, std::cref(conv), Behavior{});
}


// ******************************************************************************** unique_ptr用変換方法

/// @brief unique_ptr 等のコンバータ
export template <typename T, typename TargetConverter>
struct UniquePtrConverter {
    static_assert(IsUniquePtr<T>, "UniquePtrConverter requires T to be a unique_ptr-like type");
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
constexpr auto& getUniquePtrConverter() {
    static_assert(IsUniquePtr<T>, "getUniquePtrConverter requires T to be a unique_ptr-like type");
    using TargetConverter = decltype(getConverter<typename T::element_type>());
    static const UniquePtrConverter<T, TargetConverter> inst{};
    return inst;
}

/// @brief `unique_ptr` 型のメンバ用に `JsonField` を作成する（既定の要素コンバータを使用）。
export template <typename MemberPtr>
constexpr auto makeJsonUniquePtrField(
    MemberPtr memberPtr, const char* keyName) {
    static_assert(std::is_member_object_pointer_v<MemberPtr>,
        "makeJsonUniquePtrField requires MemberPtr to be a member object pointer");
    static_assert(IsUniquePtr<MemberPointerValueType<MemberPtr>>,
        "makeJsonUniquePtrField requires member to be a unique_ptr-like type");
    using Ptr = MemberPointerValueType<MemberPtr>;
    auto& converter = getUniquePtrConverter<Ptr>();
    using ConverterBody = std::remove_cvref_t<decltype(converter)>;
    using Behavior = RequiredFieldOmitBehavior<Ptr>;
    return JsonField<MemberPtr, ConverterBody, Behavior>(
        memberPtr, keyName, std::cref(converter), Behavior{});
}

/// @brief `unique_ptr` 型のメンバ用に `JsonField` を作成する（要素コンバータを明示的に指定する版）。
export template <typename MemberPtr, typename Converter>
constexpr auto makeJsonUniquePtrField(
    MemberPtr memberPtr, const char* keyName, const Converter& conv) {
    static_assert(std::is_member_object_pointer_v<MemberPtr>,
        "makeJsonUniquePtrField requires MemberPtr to be a member object pointer");
    static_assert(IsUniquePtr<MemberPointerValueType<MemberPtr>>,
        "makeJsonUniquePtrField requires member to be a unique_ptr-like type");
    using Behavior = RequiredFieldOmitBehavior<MemberPointerValueType<MemberPtr>>;
    return JsonField<MemberPtr, std::remove_cvref_t<Converter>, Behavior>(
        memberPtr, keyName, std::cref(conv), Behavior{});
}


// ******************************************************************************** variant用変換方法

export template <typename Variant>
struct VariantElementConverter {
    static_assert(IsStdVariant<Variant>,
        "VariantElementConverter requires Variant to be a std::variant");
    template<typename T>
    void write(JsonWriter& writer, const T& value) const {
        static const auto& conv = getConverter<std::remove_cvref_t<T>>();
        conv.write(writer, value);
    }

    void readNull(JsonParser& parser, Variant& value) const {
        if constexpr (canAssignNullptr()) {
            value = nullptr;
        }
        else {
            throw std::runtime_error("Null is not supported in variant");
        }
    }
private:
    static constexpr bool canAssignNullptr() noexcept {
        using Null = std::nullptr_t;
        return []<size_t... I>(std::index_sequence<I...>) {
            return (std::is_assignable_v<
                typename std::variant_alternative_t<I, Variant>&,
                Null
            > || ...);
        }(std::make_index_sequence<std::variant_size_v<Variant>>{});
    }
public:
    void readBool(JsonParser& parser, Variant& value) const {
        if constexpr (canAssign<bool>()) {
            parser.readTo(std::get<bool>(value));
        }
        else {
            throw std::runtime_error("Bool is not supported in variant");
        }
    }

    void readInteger(JsonParser& parser, Variant& value) const {
        if constexpr (canAssign<int>()) {
            parser.readTo(std::get<int>(value));
        }
        else {
            throw std::runtime_error("Integer is not supported in variant");
        }
    }

    void readNumber(JsonParser& parser, Variant& value) const {
        if constexpr (canAssign<double>()) {
            parser.readTo(std::get<double>(value));
        }
        else {
            throw std::runtime_error("Number is not supported in variant");
        }
    }

    void readString(JsonParser& parser, Variant& value) const {
        if constexpr (canAssign<std::string>()) {
            parser.readTo(std::get<std::string>(value));
        }
        else {
            throw std::runtime_error("String is not supported in variant");
        }
    }
private:
    template<class T>
    static constexpr bool canAssign() noexcept {
        using U = std::remove_cvref_t<T>;
        return []<size_t... I>(std::index_sequence<I...>) {
            return (std::is_same_v<U, std::remove_cvref_t<
                typename std::variant_alternative_t<I, Variant>>>
                || ...);
        }(std::make_index_sequence<std::variant_size_v<Variant>>{});
    }

public:
    void readStartArray(JsonParser& parser, Variant& value) const {
        throw std::runtime_error("Array is not supported in variant");
    }

    void readStartObject(JsonParser& parser, Variant& value) const {
        bool found = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            // Evaluate alternatives in order; stop at the first that matches
            ((void)(!found && ([&]() {
                using Alt = std::remove_cvref_t<typename std::variant_alternative_t<I, Variant>>;
                if constexpr (HasJsonFields<Alt> || (HasReadJson<Alt> && HasWriteJson<Alt>)) {
                    value = getConverter<Alt>().read(parser);
                    found = true;
                }
                return 0;
            }())), ...);
        }(std::make_index_sequence<std::variant_size_v<Variant>>{});

        if (!found) {
            throw std::runtime_error("Object is not supported in variant");
        }
    }
};

/// @brief `std::variant` を現在のトークンに基づいて読み書きするコンバータ。
/// @tparam T 対象の variant 型
export template <typename Variant, typename ElementConverter>
struct VariantConverter {
    static_assert(IsStdVariant<Variant>, "VariantConverter requires Variant to be a std::variant");
    using Value = Variant; 
    using ElementConverterT = std::remove_cvref_t<ElementConverter>;
    static_assert(std::is_base_of_v<VariantElementConverter<Variant>, ElementConverterT>,
        "ElementConverter must be VariantElementConverter<Variant> or derived from it");

    VariantConverter(ElementConverter elementConverter)
        : elementConverter_(std::move(elementConverter)) {}

    /// @brief Variant 値を JSON に書き出す。
    void write(JsonWriter& writer, const Variant& v) const {
        std::visit([&](const auto& inner) {
            elementConverter_.write(writer, inner);
        }, v);
    }

    /// @brief JSON のトークンに応じて variant を構築して返す。
    Variant read(JsonParser& parser) const {
        auto tokenType = parser.nextTokenType();
        Variant out{};
        switch (tokenType) {
        case JsonTokenType::Null:
            elementConverter_.readNull(parser, out);
            break;
        case JsonTokenType::Bool:
            elementConverter_.readBool(parser, out);
            break;
        case JsonTokenType::Integer:
            elementConverter_.readInteger(parser, out);
            break;
        case JsonTokenType::Number:
            elementConverter_.readNumber(parser, out);
            break;
        case JsonTokenType::String:
            elementConverter_.readString(parser, out);
            break;
        case JsonTokenType::StartObject:
            elementConverter_.readStartObject(parser, out);
            break;
        case JsonTokenType::StartArray:
            elementConverter_.readStartArray(parser, out);
            break;
        default:
            break;
        }
        return out;
    }

private:
    ElementConverter elementConverter_{};
};

/// @brief Variant 用の VariantConverter を構築するヘルパー（既定の ElementConverter を使用）。
export template <typename Variant>
constexpr auto makeVariantConverter() {
    static_assert(IsStdVariant<Variant>,
        "makeVariantConverter requires Variant to be a std::variant");
    using ElemConv = VariantElementConverter<Variant>;
    return VariantConverter<Variant, ElemConv>(ElemConv{});
}

/// @brief Variant 用の VariantConverter を構築するヘルパー（明示的な ElementConverter を使用）。
export template <typename Variant, typename ElemConv>
constexpr auto makeVariantConverter(ElemConv elemConv) {
    static_assert(IsStdVariant<Variant>,
        "makeVariantConverter requires Variant to be a std::variant");
    static_assert(std::is_base_of_v<VariantElementConverter<Variant>, std::remove_cvref_t<ElemConv>>,
        "ElemConv must be derived from VariantElementConverter<Variant>");
    return VariantConverter<Variant, std::remove_cvref_t<ElemConv>>(std::move(elemConv));
}

/// @brief `std::variant` 型のメンバ用に `JsonField` を作成するヘルパー（既定の `VariantConverter` を使用）。
export template <typename MemberPtr>
constexpr auto makeJsonVariantField(
    MemberPtr memberPtr, const char* keyName) {
    static_assert(std::is_member_object_pointer_v<MemberPtr>,
        "makeJsonVariantField requires MemberPtr to be a member object pointer");
    static_assert(IsStdVariant<MemberPointerValueType<MemberPtr>>,
        "makeJsonVariantField requires member to be a std::variant");
    using Var = MemberPointerValueType<MemberPtr>;
    static const auto converter = makeVariantConverter<Var>();
    using ConverterBody = std::remove_cvref_t<decltype(converter)>;
    using Behavior = RequiredFieldOmitBehavior<Var>;
    return JsonField<MemberPtr, ConverterBody, Behavior>(
        memberPtr, keyName, std::cref(converter), Behavior{});
}


// ******************************************************************************** トークン種別毎の分岐用

/// @brief トークン種別ごとの読み取り／書き出しを提供する基底的なコンバータ
export template <typename ValueType>
struct TokenConverter {
    using Value = ValueType;

    // 読み取り（各トークン種別ごとにオーバーライド可能）
    Value readNull(JsonParser& parser) const {
        if constexpr (std::is_constructible_v<Value, std::nullptr_t>) {
            parser.skipValue();
            return Value(nullptr);
        }
        else {
            throw std::runtime_error("Null is not supported for TokenConverter");
        }
    }

    Value readBool(JsonParser& parser) const {
        return this->template read<bool>(parser, "Bool is not supported for TokenConverter");
    }

    Value readInteger(JsonParser& parser) const {
        return this->template read<int>(parser, "Integer is not supported for TokenConverter");
    }

    Value readNumber(JsonParser& parser) const {
        return this->template read<double>(parser, "Number is not supported for TokenConverter");
    }

    Value readString(JsonParser& parser) const {
        return this->template read<std::string>(parser, "String is not supported for TokenConverter");
    }

    Value readStartObject(JsonParser& parser) const {
        if constexpr (HasJsonFields<Value> || (HasReadJson<Value> && HasWriteJson<Value>)) {
            return getConverter<Value>().read(parser);
        }
        else {
            throw std::runtime_error("Object is not supported for TokenConverter");
        }
    }

    Value readStartArray(JsonParser& parser) const {
        // デフォルトでは配列はサポートしない（必要なら派生で実装）
        throw std::runtime_error("Array is not supported for TokenConverter");
    }
private:
    template <typename T>
    static constexpr Value read(JsonParser& parser, const char* errorMessage) {
        if constexpr (std::is_constructible_v<Value, T>) {
            T s;
            parser.readTo(s);
            return Value(s);
        }
        else {
            throw std::runtime_error(errorMessage);
        }
    }

    // 書き出し（オーバーライド可能）
    void write(JsonWriter& writer, const Value& value) const {
        // Default: try direct writer or getConverter if available
        if constexpr (requires { writer.writeObject(value); }) {
            writer.writeObject(value);
        }
        else if constexpr (HasJsonFields<Value> || (HasReadJson<Value> && HasWriteJson<Value>)) {
            getConverter<Value>().write(writer, value);
        }
        else {
            static_assert(std::is_same_v<Value, void>,
                "TokenConverter::write: unsupported Value type");
        }
    }
};

/// @brief トークン種別に応じた分岐コンバータ（JsonTokenDispatchField と同等）
export template <typename ValueType, typename TokenConv = TokenConverter<ValueType>>
struct TokenDispatchConverter {
    using Value = ValueType; 
    using TokenConvT = std::remove_cvref_t<TokenConv>;
    static_assert(std::is_base_of_v<TokenConverter<Value>, TokenConvT>,
        "TokenConv must be TokenConverter<Value> or derived from it");

    // コンストラクタ（TokenConverter を受け取る）
    constexpr explicit TokenDispatchConverter(const TokenConvT& conv = TokenConvT())
        : tokenConverter_(conv) {}

    /// @brief トークン種別に応じて適切な変換関数を呼び出して値を読み取る。
    ValueType read(JsonParser& parser) const {
        switch (parser.nextTokenType()) {
        case JsonTokenType::Null:        return tokenConverter_.readNull(parser);
        case JsonTokenType::Bool:        return tokenConverter_.readBool(parser);
        case JsonTokenType::Integer:     return tokenConverter_.readInteger(parser);
        case JsonTokenType::Number:      return tokenConverter_.readNumber(parser);
        case JsonTokenType::String:      return tokenConverter_.readString(parser);
        case JsonTokenType::StartObject: return tokenConverter_.readStartObject(parser);
        case JsonTokenType::StartArray:  return tokenConverter_.readStartArray(parser);
        default: throw std::runtime_error("Unsupported token type");
        }
    }

    /// @brief 値を JSON に書き出すための関数を呼び出す。
    void write(JsonWriter& writer, const ValueType& value) const {
        tokenConverter_.write(writer, value);
    }

private:
    TokenConvT tokenConverter_{};
};

/// @brief 汎用の `TokenDispatchConverter` を受け取って `JsonField` を作成するヘルパー。
export template <typename MemberPtr, typename Converter>
constexpr auto makeJsonTokenDispatchField(MemberPtr memberPtr, const char* keyName,
    const Converter& conv) {
    static_assert(std::is_member_object_pointer_v<MemberPtr>,
        "makeJsonTokenDispatchField requires MemberPtr to be a member object pointer");
    static_assert(requires { typename Converter::Value; },
        "Converter must define nested type 'Value'");
    static_assert(std::same_as<typename Converter::Value, MemberPointerValueType<MemberPtr>>,
        "Converter::Value must match the member's value type");
    using Value = MemberPointerValueType<MemberPtr>;
    using Behavior = RequiredFieldOmitBehavior<Value>;
    return JsonField<MemberPtr, std::remove_cvref_t<Converter>, Behavior>(
        memberPtr, keyName, std::cref(conv), Behavior{});
}


}  // namespace rai::json
