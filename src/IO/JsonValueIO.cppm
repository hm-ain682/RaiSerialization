/// @file JsonValueIO.cppm
/// @brief writeValue/readValue ヘルパーを独立させたモジュール。

module;
#include <memory>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <variant>
#include <ranges>
#include <vector>
#include <set>
#include <stdexcept>

export module rai.json.json_value_io;

import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;

namespace rai::json::value_io {

// -----------------------------------------------------------------------------
// Paired write/read implementations (grouped by type)
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Convenience forwarding for cv/ref-qualified types

/// @brief cv/ref 修飾された型を内部で素の型（remove_cvref_t）へフォワードします。
/// @tparam T cv/ref 修飾の可能性がある型
/// @param writer 出力先の JsonWriter
/// @param value 書き出す値（cv/ref 付き）
export template <typename T>
    requires (!std::is_same_v<std::remove_cvref_t<T>, T>)
void writeValue(JsonWriter& writer, const T& value) {
    writeValue<std::remove_cvref_t<T>>(writer, value);
}

/// @brief cv/ref 修飾された型の読み取りを素の型にフォワードします。
/// @tparam T cv/ref 修飾の可能性がある型
/// @param parser 入力元の JsonParser
/// @return remove_cvref_t<T> に変換した結果を返します
export template <typename T>
    requires (!std::is_same_v<std::remove_cvref_t<T>, T>)
auto readValue(JsonParser& parser) -> std::remove_cvref_t<T> {
    return readValue<std::remove_cvref_t<T>>(parser);
} 

// -----------------------------------------------------------------------------
// Fundamental types

/// @brief 基本型（数値や真偽値など）を JSON に書き出します。
/// @tparam T 書き出す基本型
/// @param writer 出力先の JsonWriter
/// @param value 出力する値
export template <typename T>
    requires IsFundamentalValue<T>
void writeValue(JsonWriter& writer, const T& value) {
    writer.writeObject(value);
}

/// @brief JSON から基本型（数値や真偽値）を読み取ります。
/// @tparam T 読み取る型
/// @param parser 入力元の JsonParser
/// @return 読み取った値を返します
export template <typename T>
    requires IsFundamentalValue<T>
T readValue(JsonParser& parser) {
    T out{};
    parser.readTo(out);
    return out;
}

// -----------------------------------------------------------------------------
// std::string

/// @brief std::string を JSON 文字列として書き出します。
/// @tparam T 必ず std::string
/// @param writer 出力先の JsonWriter
/// @param value 書き出す文字列
export template <typename T>
    requires std::is_same_v<T, std::string>
void writeValue(JsonWriter& writer, const T& value) {
    writer.writeObject(value);
}

/// @brief JSON 文字列を std::string として読み取ります。
/// @tparam T 必ず std::string
/// @param parser 入力元の JsonParser
/// @return 読み取った std::string
export template <typename T>
    requires std::is_same_v<T, std::string>
std::string readValue(JsonParser& parser) {
    std::string out;
    parser.readTo(out);
    return out;
} 

// -----------------------------------------------------------------------------
// HasJsonFields

/// @brief jsonFields() を持つオブジェクトを JSON オブジェクトとして書き出します。
/// @tparam T jsonFields を提供する型
/// @param writer 出力先の JsonWriter
/// @param obj 書き出すオブジェクト
export template <typename T>
    requires HasJsonFields<T>
void writeValue(JsonWriter& writer, const T& obj) {
    auto& fields = obj.jsonFields();
    writer.startObject();
    fields.writeFieldsOnly(writer, static_cast<const void*>(&obj));
    writer.endObject();
}

/// @brief jsonFields() を使ってオブジェクトを JSON から読み取ります。
/// @tparam T jsonFields を提供する型
/// @param parser 入力元の JsonParser
/// @return 読み取ったオブジェクトを返します
export template <typename T>
    requires HasJsonFields<T>
T readValue(JsonParser& parser) {
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

// -----------------------------------------------------------------------------
// HasWriteJson / HasReadJson

/// @brief 型が writeJson を提供する場合に、writeJson を呼び出して書き出します。
/// @tparam T writeJson を持つ型
/// @param writer 出力先の JsonWriter
/// @param obj 書き出すオブジェクト
export template <typename T>
    requires HasWriteJson<T>
void writeValue(JsonWriter& writer, const T& obj) {
    obj.writeJson(writer);
}

/// @brief 型が readJson を提供する場合に、readJson を呼び出して読み取ります。
/// @tparam T readJson を持つ型
/// @param parser 入力元の JsonParser
/// @return 読み取ったオブジェクト
export template <typename T>
    requires HasReadJson<T>
T readValue(JsonParser& parser) {
    T out{};
    out.readJson(parser);
    return out;
} 

// -----------------------------------------------------------------------------
// Unique pointers

/// @brief std::unique_ptr 等のポインタを JSON に書き出します。nullptr は JSON の null になります。
/// @tparam T ポインタ型（std::unique_ptr 等）
/// @param writer 出力先の JsonWriter
/// @param ptr 書き出すポインタ
export template <typename T>
    requires IsUniquePtr<T>
void writeValue(JsonWriter& writer, const T& ptr) {
    if (!ptr) {
        writer.null();
        return;
    }
    writeValue(writer, *ptr);
}

/// @brief std::unique_ptr 等を JSON から読み取ります。JSON の null は nullptr に変換されます。
/// @tparam T ポインタ型（std::unique_ptr 等）
/// @param parser 入力元の JsonParser
/// @return 読み取ったポインタを返します（成功時は std::make_unique による所有権を持つ）
export template <typename T>
    requires IsUniquePtr<T>
T readValue(JsonParser& parser) {
    using Element = typename T::element_type;
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }
    auto elem = readValue<Element>(parser);
    return std::make_unique<Element>(std::move(elem));
}  

// -----------------------------------------------------------------------------
// Variant

/// @brief variant の代替型を走査して、トークンにマッチする代替型を読み取る内部ヘルパーです。
/// @tparam VariantType std::variant 型
/// @tparam Is index_sequence のインデックス列
/// @param parser 入力元の JsonParser
/// @param tokenType 現在の JSON トークン
/// @return 読み取られた VariantType を返します
template <typename VariantType, std::size_t... Is>
VariantType readVariantByIndex(JsonParser& parser, JsonTokenType tokenType,
    std::index_sequence<Is...>) {
    VariantType out;
    bool matched = false;
    auto helper = [&](auto idx) {
        if (matched) {
            return;
        }
        constexpr std::size_t I = decltype(idx)::value;
        using Alternative = std::variant_alternative_t<I, VariantType>;
        if (isVariantAlternativeMatch<Alternative>(tokenType)) {
            out = VariantType(std::in_place_index<I>, readValue<Alternative>(parser));
            matched = true;
        }
    };
    (helper(std::integral_constant<std::size_t, Is>{}), ...);
    if (!matched) {
        throw std::runtime_error("Failed to dispatch variant for current token");
    }
    return out;
}

/// @brief トークンが型 T の代替型に対応しているか判定する内部ユーティリティ。
/// @tparam T 判定対象の型
/// @param tokenType 現在の JSON トークン
/// @return マッチする場合は true
template <typename T>
bool isVariantAlternativeMatch(JsonTokenType tokenType) {
    using Decayed = std::remove_cvref_t<T>;
    switch (tokenType) {
    case JsonTokenType::Null:
        return IsUniquePtr<Decayed>;    case JsonTokenType::Bool:
        return std::is_same_v<Decayed, bool>;
    case JsonTokenType::Integer:
        return std::is_integral_v<Decayed> && !std::is_same_v<Decayed, bool>;
    case JsonTokenType::Number:
        return std::is_floating_point_v<Decayed>;
    case JsonTokenType::String:
        return std::is_same_v<Decayed, std::string>;
    case JsonTokenType::StartObject:
        return HasJsonFields<Decayed> || HasReadJson<Decayed> || IsUniquePtr<Decayed>;
    case JsonTokenType::StartArray:
        return IsStdVector<Decayed>;
    default:
        return false;
    }
}

/// @brief std::variant を JSON に書き出します（現在の代替型に応じて writeValue を再帰呼び出し）。
/// @tparam T std::variant 型
/// @param writer 出力先の JsonWriter
/// @param v 書き出す variant
export template <typename T>
    requires IsStdVariant<T>
void writeValue(JsonWriter& writer, const T& v) {
    std::visit([&writer](const auto& inner) {
        writeValue(writer, inner);
    }, v);
}

/// @brief std::variant を JSON から読み取ります（トークンに基づいて代替型を選択）。
/// @tparam T std::variant 型
/// @param parser 入力元の JsonParser
/// @return 読み取った variant を返します
export template <typename T>
    requires IsStdVariant<T>
T readValue(JsonParser& parser) {
    using VariantType = T;
    auto tokenType = parser.nextTokenType();
    return readVariantByIndex<VariantType>(parser, tokenType,
        std::make_index_sequence<std::variant_size_v<VariantType>>{});
}

// -----------------------------------------------------------------------------
// Ranges (exclude stringlike)

/// @brief ranges に準拠するコンテナを JSON 配列として書き出します（文字列ライク型は除外）。
/// @tparam T ranges に準拠するコンテナ型
/// @param writer 出力先の JsonWriter
/// @param range 書き出す範囲
export template <typename T>
    requires IsContainer<T>
void writeValue(JsonWriter& writer, const T& range) {
    writer.startArray();
    for (const auto& elem : range) {
        writeValue(writer, elem);
    }
    writer.endArray();
}

/// @brief push_back をサポートするコンテナを JSON 配列から読み取ります。
/// @tparam T push_back をサポートするコンテナ型
/// @param parser 入力元の JsonParser
/// @return 読み取ったコンテナを返します
export template <typename T>
    requires (IsContainer<T> &&
        requires(T& c, std::ranges::range_value_t<T>&& v) { c.push_back(std::move(v)); })
T readValue(JsonParser& parser) {
    using Element = std::ranges::range_value_t<T>;
    T out{};
    parser.startArray();
    while (!parser.nextIsEndArray()) {
        out.push_back(readValue<Element>(parser));
    }
    parser.endArray();
    return out;
}

/// @brief insert をサポートする集合系コンテナを JSON 配列から読み取ります。
/// @tparam T insert をサポートするコンテナ型（例: std::set）
/// @param parser 入力元の JsonParser
/// @return 読み取ったコンテナを返します
export template <typename T>
    requires (IsContainer<T> &&
        requires(T& c, std::ranges::range_value_t<T>&& v) { c.insert(std::move(v)); })
T readValue(JsonParser& parser) {
    using Element = std::ranges::range_value_t<T>;
    T out{};
    parser.startArray();
    while (!parser.nextIsEndArray()) {
        out.insert(readValue<Element>(parser));
    }
    parser.endArray();
    return out;
}

} // namespace rai::json::value_io
