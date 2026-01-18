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
// Fundamental types
export template <typename T>
    requires IsFundamentalValue<T>
void writeValue(JsonWriter& writer, const T& value) {
    writer.writeObject(value);
}

export template <typename T>
    requires IsFundamentalValue<std::remove_cvref_t<T>>
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    std::remove_cvref_t<T> out{};
    parser.readTo(out);
    return out;
}

// -----------------------------------------------------------------------------
// std::string
export template <typename T>
    requires std::is_same_v<std::remove_cvref_t<T>, std::string>
void writeValue(JsonWriter& writer, const T& value) {
    writer.writeObject(value);
}

export template <typename T>
    requires std::is_same_v<std::remove_cvref_t<T>, std::string>
std::string readValue(JsonParser& parser) {
    std::string out;
    parser.readTo(out);
    return out;
}

// -----------------------------------------------------------------------------
// HasJsonFields
export template <typename T>
    requires HasJsonFields<T>
void writeValue(JsonWriter& writer, const T& obj) {
    auto& fields = obj.jsonFields();
    writer.startObject();
    fields.writeFieldsOnly(writer, static_cast<const void*>(&obj));
    writer.endObject();
}

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

export template <typename T>
    requires HasJsonFields<std::remove_cvref_t<T>>
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    return readObjectWithFields<std::remove_cvref_t<T>>(parser);
}

// -----------------------------------------------------------------------------
// HasWriteJson / HasReadJson
export template <typename T>
    requires HasWriteJson<T>
void writeValue(JsonWriter& writer, const T& obj) {
    obj.writeJson(writer);
}

export template <typename T>
    requires HasReadJson<std::remove_cvref_t<T>>
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    std::remove_cvref_t<T> out{};
    out.readJson(parser);
    return out;
}

// -----------------------------------------------------------------------------
// Unique pointers
export template <typename T>
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
        writeValue(writer, *ptr);
    }
}

export template <typename T>
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
        auto elem = readValue<Element>(parser);
        return std::make_unique<Element>(std::move(elem));
    }
}

// -----------------------------------------------------------------------------
// Variant

export template <typename T>
auto readValue(JsonParser& parser) -> std::remove_cvref_t<T>;
template <typename VariantType, std::size_t... Is>
VariantType readVariantByIndex(JsonParser& parser, JsonTokenType tokenType, std::index_sequence<Is...>) {
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
    if (!matched) throw std::runtime_error("Failed to dispatch variant for current token");
    return out;
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

export template <typename T>
    requires IsStdVariant<std::remove_cvref_t<T>>::value
void writeValue(JsonWriter& writer, const T& v) {
    std::visit([&writer](const auto& inner) { writeValue(writer, inner); }, v);
}

export template <typename T>
    requires IsStdVariant<std::remove_cvref_t<T>>::value
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    using VariantType = std::remove_cvref_t<T>;
    auto tokenType = parser.nextTokenType();
    return readVariantByIndex<VariantType>(parser, tokenType, std::make_index_sequence<std::variant_size_v<VariantType>>{});
}

// -----------------------------------------------------------------------------
// Ranges (exclude stringlike)
export template <typename T>
    requires (std::ranges::range<T> && !StringLike<T>)
void writeValue(JsonWriter& writer, const T& range) {
    writer.startArray();
    for (const auto& elem : range) {
        writeValue(writer, elem);
    }
    writer.endArray();
}

// readValue for containers that support push_back
export template <typename T>
    requires (std::ranges::range<std::remove_cvref_t<T>> && !StringLike<std::remove_cvref_t<T>> &&
              requires(std::remove_cvref_t<T>& c, std::ranges::range_value_t<std::remove_cvref_t<T>>&& v) { c.push_back(std::move(v)); })
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    using Decayed = std::remove_cvref_t<T>;
    using Element = std::ranges::range_value_t<Decayed>;
    Decayed out{};
    parser.startArray();
    while (!parser.nextIsEndArray()) {
        out.push_back(readValue<Element>(parser));
    }
    parser.endArray();
    return out;
} 

// readValue for containers that support insert
export template <typename T>
    requires (std::ranges::range<std::remove_cvref_t<T>> && !StringLike<std::remove_cvref_t<T>> &&
              requires(std::remove_cvref_t<T>& c, std::ranges::range_value_t<std::remove_cvref_t<T>>&& v) { c.insert(std::move(v)); })
std::remove_cvref_t<T> readValue(JsonParser& parser) {
    using Decayed = std::remove_cvref_t<T>;
    using Element = std::ranges::range_value_t<Decayed>;
    Decayed out{};
    parser.startArray();
    while (!parser.nextIsEndArray()) {
        out.insert(readValue<Element>(parser));
    }
    parser.endArray();
    return out;
}

} // namespace rai::json::value_io
