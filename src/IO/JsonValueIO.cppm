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

export namespace rai::json::value_io {

// 値の書き出し（基本型）
template <typename T>
    requires IsFundamentalValue<T>
void writeValue(JsonWriter& writer, const T& value) {
    writer.writeObject(value);
}

// 文字列
template <typename T>
    requires std::is_same_v<std::remove_cvref_t<T>, std::string>
void writeValue(JsonWriter& writer, const T& value) {
    writer.writeObject(value);
}

// unique_ptr
template <typename T>
    requires UniquePointer<T>
void writeValue(JsonWriter& writer, const T& ptr) {
    if (!ptr) {
        writer.null();
        return;
    }
    using Element = typename PointerElementType<std::remove_cvref_t<T>>::type;
    if constexpr (HasJsonFields<Element>) {
        auto& fields = ptr->jsonFields();
        writer.startObject();
        fields.writeFieldsOnly(writer, ptr.get());
        writer.endObject();
    } else {
        value_io::writeValue(writer, *ptr);
    }
}

// variant
template <typename T>
    requires IsStdVariant<std::remove_cvref_t<T>>::value
void writeValue(JsonWriter& writer, const T& v) {
    std::visit([&writer](const auto& inner) { value_io::writeValue(writer, inner); }, v);
}

// ranges (exclude stringlike)
template <typename T>
    requires (std::ranges::range<T> && !StringLike<T>)
void writeValue(JsonWriter& writer, const T& range) {
    writer.startArray();
    for (const auto& elem : range) {
        value_io::writeValue(writer, elem);
    }
    writer.endArray();
}

// HasJsonFields
template <typename T>
    requires HasJsonFields<T>
void writeValue(JsonWriter& writer, const T& obj) {
    auto& fields = obj.jsonFields();
    writer.startObject();
    fields.writeFieldsOnly(writer, static_cast<const void*>(&obj));
    writer.endObject();
}

// HasWriteJson
template <typename T>
    requires HasWriteJson<T>
void writeValue(JsonWriter& writer, const T& obj) {
    obj.writeJson(writer);
}

// -----------------------------------------------------------------------------
// 読み取り側
// -----------------------------------------------------------------------------

// Forward declarations for helpers used by readValue implementations

template <typename T>
    requires IsFundamentalValue<std::remove_cvref_t<T>>
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    std::remove_cvref_t<T> out{};
    parser.readTo(out);
    return out;
}

template <typename T>
    requires HasReadJson<std::remove_cvref_t<T>>
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    std::remove_cvref_t<T> out{};
    out.readJson(parser);
    return out;
}

template <typename T>
    requires std::is_same_v<std::remove_cvref_t<T>, std::string>
std::string readValue(JsonParser& parser) {
    std::string out;
    parser.readTo(out);
    return out;
}

// helpers
template <typename T>
T readObjectWithFields(JsonParser& parser) {
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

template <typename T>
    requires HasJsonFields<std::remove_cvref_t<T>>
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    return readObjectWithFields<std::remove_cvref_t<T>>(parser);
}

template <typename T>
    requires UniquePointer<std::remove_cvref_t<T>>
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    using Element = typename PointerElementType<std::remove_cvref_t<T>>::type;
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }
    if constexpr (std::is_same_v<Element, std::string>) {
        std::string tmp;
        parser.readTo(tmp);
        return std::make_unique<Element>(std::move(tmp));
    } else {
        auto elem = value_io::readValue<Element>(parser);
        return std::make_unique<Element>(std::move(elem));
    }
}

template <typename T>
    requires (std::ranges::range<std::remove_cvref_t<T>> && !StringLike<std::remove_cvref_t<T>>)
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    using Decayed = std::remove_cvref_t<T>;
    using Element = std::ranges::range_value_t<Decayed>;
    Decayed out{};
    parser.startArray();
    while (!parser.nextIsEndArray()) {
        if constexpr (std::is_same_v<Element, std::string>) {
            std::string tmp;
            parser.readTo(tmp);
            if constexpr (requires(Decayed& c, Element&& v) { c.push_back(std::move(v)); }) {
                out.push_back(std::move(tmp));
            } else if constexpr (requires(Decayed& c, Element&& v) { c.insert(std::move(v)); }) {
                out.insert(std::move(tmp));
            } else {
                static_assert(AlwaysFalse<Decayed>, "Container must support push_back or insert");
            }
        } else {
            if constexpr (requires(Decayed& c, Element&& v) { c.push_back(std::move(v)); }) {
                out.push_back(value_io::readValue<Element>(parser));
            } else if constexpr (requires(Decayed& c, Element&& v) { c.insert(std::move(v)); }) {
                out.insert(value_io::readValue<Element>(parser));
            } else {
                static_assert(AlwaysFalse<Decayed>, "Container must support push_back or insert");
            }
        }
    }
    parser.endArray();
    return out;
}

template <typename VariantType, std::size_t Index = 0>
VariantType readVariantImpl(JsonParser& parser, JsonTokenType tokenType) {
    constexpr std::size_t AlternativeCount = std::variant_size_v<VariantType>;
    if constexpr (Index >= AlternativeCount) {
        throw std::runtime_error("Failed to dispatch variant for current token");
    }
    else {
        using Alternative = std::variant_alternative_t<Index, VariantType>;
        if (isVariantAlternativeMatch<Alternative>(tokenType)) {
            return VariantType(std::in_place_index<Index>, value_io::readValue<Alternative>(parser));
        }
        return readVariantImpl<VariantType, Index + 1>(parser, tokenType);
    }
}

template <typename T>
    requires IsStdVariant<std::remove_cvref_t<T>>::value
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    auto tokenType = parser.nextTokenType();
    return readVariantImpl<std::remove_cvref_t<T>>(parser, tokenType);
}

template <typename T>
bool isVariantAlternativeMatch(JsonTokenType tokenType) {
    using Decayed = std::remove_cvref_t<T>;
    switch (tokenType) {
    case JsonTokenType::Null:
        return UniquePointer<Decayed>;
    case JsonTokenType::Bool:
        return std::is_same_v<Decayed, bool>;
    case JsonTokenType::Integer:
        return std::is_integral_v<Decayed> && !std::is_same_v<Decayed, bool>;
    case JsonTokenType::Number:
        return std::is_floating_point_v<Decayed>;
    case JsonTokenType::String:
        return std::is_same_v<Decayed, std::string>;
    case JsonTokenType::StartObject:
        return HasJsonFields<Decayed> || HasReadJson<Decayed> || UniquePointer<Decayed>;
    case JsonTokenType::StartArray:
        return IsStdVector<Decayed>::value;
    default:
        return false;
    }
}

// variant dispatch helpers are intentionally not duplicated here; reuse those in JsonField module.

} // namespace rai::json::value_io
