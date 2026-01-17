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



/// @note nullは全ポインタ型（unique_ptr/shared_ptr/生ポインタ）へ暗黙変換可能のため、
///       専用ヘルパーは不要（nullptrを直接返す）。

// ******************************************************************************** フィールド定義

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

protected:
    // Value IO helpers moved to rai::json::value_io namespace
    // writeValue/readValue implementations were extracted to a separate module.




private:
    // Variant/object helpers moved to rai::json::value_io namespace.
};

// Forward declaration of JsonField (primary template)
export template <typename MemberPtrType>
struct JsonField;

// Deduction guides for constructing JsonField from constructor arguments
export template <typename MemberPtrType>
JsonField(MemberPtrType, const char*, bool) -> JsonField<MemberPtrType>;
export template <typename MemberPtrType>
JsonField(MemberPtrType, const char*) -> JsonField<MemberPtrType>;

// ------------------------------
// JsonField partial specializations
// ------------------------------
// Specialization: std::string
/// Handles string member variables. Writes/reads string values directly.
export template <typename Owner, typename Value>
    requires std::same_as<std::remove_cvref_t<Value>, std::string>
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        writer.writeObject(value);
    }

    ValueType fromJson(JsonParser& parser) const {
        ValueType out;
        parser.readTo(out);
        return out;
    }
};

// Specialization: fundamental types (int/float/bool/...)
/// Uses fundamental read/write helpers (readTo/writeObject).
export template <typename Owner, typename Value>
    requires IsFundamentalValue<std::remove_cvref_t<Value>>
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        writer.writeObject(value);
    }

    ValueType fromJson(JsonParser& parser) const {
        ValueType out{};
        parser.readTo(out);
        return out;
    }
};

// Specialization: unique_ptr / smart pointer types
/// Handles pointer types; supports null and object/string payloads.
export template <typename Owner, typename Value>
    requires UniquePointer<std::remove_cvref_t<Value>>
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        ::rai::json::value_io::template writeValue<ValueType>(writer, value);
    }

    ValueType fromJson(JsonParser& parser) const {
        return ::rai::json::value_io::template readValue<ValueType>(parser);
    }
};

// Specialization: std::variant
/// Dispatches variant alternatives using readVariant/writeValue.
export template <typename Owner, typename Value>
    requires IsStdVariant<std::remove_cvref_t<Value>>::value
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        ::rai::json::value_io::template writeValue<ValueType>(writer, value);
    }

    ValueType fromJson(JsonParser& parser) const {
        return ::rai::json::value_io::template readValue<ValueType>(parser);
    }
};

// Specialization: ranges (containers like vector, set) excluding string
/// Generic container handling; requires push_back or insert for element insertion.
export template <typename Owner, typename Value>
    requires (std::ranges::range<std::remove_cvref_t<Value>> && !StringLike<std::remove_cvref_t<Value>>)
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        ::rai::json::value_io::template writeValue<ValueType>(writer, value);
    }

    ValueType fromJson(JsonParser& parser) const {
        return ::rai::json::value_io::template readValue<ValueType>(parser);
    }
};

// Specialization: types with jsonFields() or custom readJson/writeJson
/// Delegates to object-level jsonFields/readJson/writeJson implementations.
export template <typename Owner, typename Value>
    requires (HasJsonFields<std::remove_cvref_t<Value>> ||
              HasReadJson<std::remove_cvref_t<Value>> ||
              HasWriteJson<std::remove_cvref_t<Value>>)
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        ::rai::json::value_io::template writeValue<ValueType>(writer, value);
    }

    ValueType fromJson(JsonParser& parser) const {
        return ::rai::json::value_io::template readValue<ValueType>(parser);
    }
};

// Fallback generic partial specialization (catch-all)
/// Generic fallback that uses writeValue/readValue helpers.
export template <typename Owner, typename Value>
struct JsonField<Value Owner::*> : JsonFieldBase<Value Owner::*> {
    using Base = JsonFieldBase<Value Owner::*>;
    using ValueType = typename Base::ValueType;

    constexpr explicit JsonField(Value Owner::* memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    void toJson(JsonWriter& writer, const ValueType& value) const {
        ::rai::json::value_io::template writeValue<ValueType>(writer, value);
    }

    ValueType fromJson(JsonParser& parser) const {
        return ::rai::json::value_io::template readValue<ValueType>(parser);
    }
};


}  // namespace rai::json
