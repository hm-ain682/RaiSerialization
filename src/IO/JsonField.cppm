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

/// @brief JSONフィールドの基本定義。
/// @tparam MemberPtr メンバー変数へのポインタ。
export template <typename MemberPtrType>
struct JsonFieldBase {
    static_assert(std::is_member_object_pointer_v<MemberPtrType>,
        "JsonField requires a data member pointer");
    using Traits = MemberPointerTraits<MemberPtrType>;
    using OwnerType = typename Traits::OwnerType;
    using ValueType = typename Traits::ValueType;

    /// @brief コンストラクタ。
    /// @param memberPtr メンバー変数へのポインタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    constexpr explicit JsonFieldBase(MemberPtrType memberPtr, const char* keyName, bool req = false)
        : member(memberPtr), key(keyName), required(req) {}

    MemberPtrType member{}; ///< メンバー変数へのポインタ。
    const char* key{};      ///< JSONキー名。
    bool required{false};   ///< 必須フィールドかどうか。
};

// ******************************************************************************** 基本型用

// Forward declaration of JsonField (primary template)
export template <typename MemberPtrType>
struct JsonField;

// Deduction guides for constructing JsonField from constructor arguments
export template <typename MemberPtrType>
JsonField(MemberPtrType, const char*, bool) -> JsonField<MemberPtrType>;
export template <typename MemberPtrType>
JsonField(MemberPtrType, const char*) -> JsonField<MemberPtrType>;

/// @brief `value_io` が直接扱える型群を表す concept。
/// @note 新しい型サポートを追加する際は、`value_io` 側の実装と
///       両方を更新してください。これにより診断が早期に行われます。
export template<typename T>
concept ValueIoSupported
    = IsFundamentalValue<T>         // 基本型（数値/真偽値 等）
    || std::same_as<T, std::string> // std::string
    || IsUniquePtr<T>               // std::unique_ptr等のポインタ
    || IsStdVariant<T>               // std::variant
    || IsRangeContainer<T>          // vector/set等のコンテナ
    || HasJsonFields<T>             // `jsonFields()` を持つ型
    || (HasReadJson<T> && HasWriteJson<T>); // `readJson` / `writeJson` を持つ型

/// @brief `value_io` に処理を委譲する `JsonField` の部分特殊化。
/// @details `ValueIoSupported` を満たす型のみを受け付け、
///          `value_io::writeValue` / `value_io::readValue` に処理を委ねます。
///          未対応型は concept によってコンパイル時に早期に検出され、
///          エラーの発生箇所と原因が明確になります。
export template <typename Owner, typename Value>
    requires ValueIoSupported<std::remove_cvref_t<Value>>
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        value_io::template writeValue<ValueType>(writer, value);
    }

    ValueType fromJson(JsonParser& parser) const {
        return value_io::template readValue<ValueType>(parser);
    }
};

/// @brief JsonField を生成するヘルパー関数（CTAD 回避用）
export template <typename MemberPtrType>
constexpr auto makeJsonField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    return JsonField<MemberPtrType>(memberPtr, keyName, req);
}

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

/// @brief JsonEnumMap を使い、実体を外部で共有する JsonEnumField（仮想は使わない）。
/// @tparam MemberPtrType メンバポインタ型
/// @tparam MapType JsonEnumMap の型
export template <typename MemberPtrType, IsJsonEnumMap MapType>
struct JsonEnumField : JsonFieldBase<MemberPtrType> {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using ValueType = typename Traits::ValueType;
    static_assert(std::is_enum_v<ValueType>, "JsonEnumField requires enum type");
    static_assert(std::is_same_v<typename MapType::Enum, ValueType>, "MapType must map the same enum type");

    /// @brief コンストラクタ（既存の JsonEnumMap を参照で受け取る）
    constexpr explicit JsonEnumField(MemberPtrType memberPtr, const char* keyName,
        const MapType& map, bool req = false)
        : JsonFieldBase<MemberPtrType>(memberPtr, keyName, req), map_(&map) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        if (auto name = map_->toName(value)) {
            writer.writeObject(*name);
            return;
        }
        throw std::runtime_error("Failed to convert enum to string");
    }

    ValueType fromJson(JsonParser& parser) const {
        std::string jsonValue;
        parser.readTo(jsonValue);
        if (auto v = map_->fromName(jsonValue)) {
            return *v;
        }
        throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
    }

private:
    const MapType* map_{}; ///< 外部で定義されたマップへの参照（ポインタで保持）
};

/// @brief JsonEnumMap の作成ヘルパー（配列から型を推論して作る）
export template <typename Enum, std::size_t N>
constexpr auto makeJsonEnumMap(const EnumEntry<Enum> (&entries)[N]) {
    return JsonEnumMap<Enum, N>(entries);
}

/// @brief JsonEnumField を簡単に作るヘルパー（MapType を推論）
export template <typename MemberPtrType, typename MapType>
constexpr auto makeJsonEnumField(MemberPtrType memberPtr, const char* keyName,
    const MapType& map, bool req = false) {
    return JsonEnumField<MemberPtrType, MapType>(memberPtr, keyName, map, req);
}

// ******************************************************************************** コンテナ用

/// @brief 配列形式のJSONを読み書きする汎用フィールド。
/// @tparam MemberPtrType コンテナ型のメンバー変数へのポインタ。
/// @details push_back または insert を持つコンテナに対応する。
export template <typename MemberPtrType>
    requires std::ranges::range<typename MemberPointerTraits<MemberPtrType>::ValueType>
struct JsonSetField : JsonField<MemberPtrType> {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using ValueType = typename Traits::ValueType;
    using Base = JsonField<MemberPtrType>;
    using ElementType = std::ranges::range_value_t<ValueType>; ///< コンテナの要素型。

    /// @brief コンストラクタ。
    /// @param memberPtr メンバー変数へのポインタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    constexpr explicit JsonSetField(MemberPtrType memberPtr, const char* keyName,
        bool req = false)
        : Base(memberPtr, keyName, req) {}

    /// @brief JsonParserから配列を読み取る。
    /// @param parser JsonParserの参照。
    /// @return 読み取ったコンテナ。
    ValueType fromJson(JsonParser& parser) const {
        ValueType out{};
        parser.startArray();
        while (!parser.nextIsEndArray()) {
            if constexpr (std::is_same_v<ElementType, std::string>) {
                std::string s;
                parser.readTo(s);
                addElement(out, std::move(s));
            } else {
                auto elem = value_io::readValue<ElementType>(parser);
                addElement(out, std::move(elem));
            }
        }
        parser.endArray();
        return out;
    }

    /// @brief JsonWriterに配列を書き出す。
    /// @param writer JsonWriterの参照。
    /// @param container 書き出すコンテナ。
    void toJson(JsonWriter& writer, const ValueType& container) const {
        writer.startArray();
        for (const auto& elem : container) {
            value_io::writeValue<ElementType>(writer, elem);
        }
        writer.endArray();
    }

private:
    /// @brief コンテナへ要素を追加する。
    /// @param container 追加先のコンテナ。
    /// @param elem 追加する要素。
    static void addElement(ValueType& container, ElementType elem) {
        if constexpr (requires(ValueType& c, ElementType&& v) { c.push_back(std::move(v)); }) {
            container.push_back(std::move(elem));
        }
        else if constexpr (requires(ValueType& c, ElementType&& v) { c.insert(std::move(v)); }) {
            container.insert(std::move(elem));
        }
        else {
            static_assert(AlwaysFalse<ValueType>, "Container must support push_back or insert");
        }
    }
};

/// @brief JsonSetFieldを生成するヘルパー関数。
/// @tparam MemberPtrType メンバーポインタ型。
/// @param memberPtr メンバー変数へのポインタ。
/// @param keyName JSONキー名。
/// @param req 必須フィールドかどうか。
/// @return 生成されたJsonSetField。
export template <typename MemberPtrType>
constexpr auto makeJsonSetField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    return JsonSetField<MemberPtrType>(memberPtr, keyName, req);
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

/// @brief JSONトークン種別に応じて変換処理を切り替えるフィールド。
/// @tparam MemberPtrType メンバー変数へのポインタ型。
/// @details fromJsonでは次のトークン種別に対応するコンバータを使用して値を読み取る。
///          toJsonでは指定されたコンバータを使用して書き出す。
export template <typename MemberPtrType>
struct JsonTokenDispatchField : JsonFieldBase<MemberPtrType> {
    using Base = JsonFieldBase<MemberPtrType>;
    using ValueType = typename MemberPointerTraits<MemberPtrType>::ValueType;
    using ToConverter = std::function<void(JsonWriter&, const ValueType&)>;

    /// @brief コンストラクタ。
    /// @param memberPtr メンバーポインタ。
    /// @param keyName JSONキー名。
    /// @param fromEntries トークン種別をインデックスとする読み取りコンバータ配列（要素数はJsonTokenTypeCount以下）。
    /// @param toConverter 書き出し用コンバータ関数。省略時はJsonWriter::writeObjectで書き出す。
    /// @param req 必須フィールドかどうか。
    /// @note fromEntries[i]はJsonTokenType(i)に対応するコンバータ。対応しないインデックスにはnullptrを設定可能。
    template <std::size_t FromN>
    explicit JsonTokenDispatchField(MemberPtrType memberPtr, const char* keyName,
        const std::array<FromJsonEntry<ValueType>, FromN>& fromEntries,
        ToConverter toConverter = defaultToConverter(),
        bool req = false)
        : Base(memberPtr, keyName, req), toConverter_(std::move(toConverter)) {
        static_assert(FromN <= JsonTokenTypeCount);
        // まず全要素を例外を投げる関数で初期化
        for (std::size_t i = 0; i < JsonTokenTypeCount; ++i) {
            fromEntries_[i] = [](JsonParser&) -> ValueType {
                throw std::runtime_error("No converter found for token type");
            };
        }
        // fromEntriesの有効なエントリをコピー（配列添え字＝トークン種別）
        for (std::size_t i = 0; i < FromN; ++i) {
            if (fromEntries[i]) {
                fromEntries_[i] = fromEntries[i];
            }
        }
    }

    /// @brief JSONから値を読み取る。
    /// @param parser JsonParserの参照。
    /// @return 読み取った値。
    /// @throws std::runtime_error 対応するコンバータが見つからない場合。
    ValueType fromJson(JsonParser& parser) const {
        // トークン種別をインデックスとして直接アクセス
        std::size_t index = static_cast<std::size_t>(parser.nextTokenType());
        return fromEntries_[index](parser);
    }

    /// @brief JSONに値を書き出す。
    /// @param writer JsonWriterの参照。
    /// @param value 書き出す値。
    void toJson(JsonWriter& writer, const ValueType& value) const {
        toConverter_(writer, value);
    }

    /// @brief 書き出し用コンバータを設定する。
    /// @param toConverter 設定するコンバータ関数。
    void setToConverter(ToConverter toConverter) {
        toConverter_ = std::move(toConverter);
    }

    /// @brief 既定の書き出し用コンバータ。
    /// @return JsonWriter::writeObjectで値を書き出すコンバータ関数。
    static ToConverter defaultToConverter() {
        return [](JsonWriter& writer, const ValueType& value) {
            writer.writeObject(value);
        };
    }

private:
    std::array<std::function<ValueType(JsonParser&)>, JsonTokenTypeCount> fromEntries_{}; ///< トークン種別をインデックスとする読み取り関数配列。
    ToConverter toConverter_; ///< 書き出し用コンバータ関数。
};

}  // namespace rai::json
