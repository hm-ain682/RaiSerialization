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

export module rai.serialization.json_field;

import rai.serialization.json_converter;
import rai.serialization.json_writer;
import rai.serialization.json_parser;
import rai.serialization.json_token_manager;

import rai.collection.sorted_hash_array_map;

namespace rai::serialization {

// ******************************************************************************** 省略時挙動

/// @brief メンバーポインタの特性を抽出するメタ関数。
/// @tparam T メンバーポインタ型。
template <typename T>
struct MemberPointerTraits;

template <typename OwnerType, typename ValueType>
struct MemberPointerTraits<ValueType OwnerType::*> {
    using Owner = OwnerType;
    using Value = ValueType;
};

/// @brief メンバーポインタ型から対応する値型を取り出す型エイリアス。
export template <typename MemberPtr>
using MemberPointerValueType = typename MemberPointerTraits<MemberPtr>::Value;

/// @brief SingleValueFieldOmitBehavior を利用できるか判定するconcept。
/// @tparam Value 対象型
template <typename Value>
concept IsSingleValueBehaviorAllowed = std::is_copy_constructible_v<Value>
    && std::is_copy_assignable_v<Value>
    && std::equality_comparable<Value>
    && (!IsContainer<Value>
        || (std::is_copy_constructible_v<std::remove_cvref_t<std::ranges::range_value_t<Value>>>
            && std::is_copy_assignable_v<std::remove_cvref_t<std::ranges::range_value_t<Value>>>))
    && (!IsUniquePtr<Value>);

/// @brief 読み込み時省略では既定値を設定し、書き出し時既定値と一致するならスキップする省略時挙動。
/// @tparam Value 値型
export template <typename Value>
struct SingleValueFieldOmitBehavior {
    static_assert(IsSingleValueBehaviorAllowed<Value>,
        "SingleValueFieldOmitBehavior requires copyable Value");
    static_assert(std::equality_comparable<Value>,
        "SingleValueFieldOmitBehavior requires equality comparable Value");

    /// @brief 値が省略条件に一致するかを返す。
    /// @param value チェックする値
    /// @return 省略すべきなら true
    bool shouldSkipWrite(const Value& value) const {
        return value == defaultValue;
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param outValue 欠落時に代入する対象
    /// @param key 対象キー名
    void applyMissing(Value& outValue, std::string_view key) const {
        (void)key;
        outValue = defaultValue;
    }

    Value defaultValue{};  ///< 省略判定と欠落時代入に使う値
};

/// @brief 省略時挙動（既定値も省略条件も持たない）。
/// @tparam Value 値型
export template <typename Value>
struct NoDefaultFieldOmitBehavior {
    /// @brief 値が省略条件に一致するかを返す。
    /// @param value チェックする値
    /// @return 省略すべきなら true
    bool shouldSkipWrite(const Value& value) const {
        (void)value;
        return false;
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param outValue 欠落時に代入する対象
    /// @param key 対象キー名
    void applyMissing(Value& outValue, std::string_view key) const {
        (void)outValue;
        (void)key;
    }
};

/// @brief 読み込み時省略では例外を送出し、常時書き出しす省略時挙動。
/// @tparam Value 値型
export template <typename Value>
struct RequiredFieldOmitBehavior {
    /// @brief 値が省略条件に一致するかを返す。
    /// @param value チェックする値
    /// @return 省略すべきなら true
    bool shouldSkipWrite(const Value& value) const {
        (void)value;
        return false;
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param outValue 欠落時に代入する対象
    /// @param key 対象キー名
    void applyMissing(Value& outValue, std::string_view key) const {
        (void)outValue;
        throw std::runtime_error(
            std::string("JsonParser: missing required key '") +
            std::string(key) + "'");
    }
};

// ******************************************************************************** フィールド

/// @brief JsonFieldの省略時挙動を満たす型の concept。
/// @tparam Behavior 挙動型
/// @tparam Value 値型
export template <typename Behavior, typename Value>
concept IsJsonFieldOmittedBehavior = requires(const Behavior& behavior, const Value& value,
    Value& outValue, std::string_view key) {
    { behavior.shouldSkipWrite(value) } -> std::same_as<bool>;
    { behavior.applyMissing(outValue, key) } -> std::same_as<void>;
};

/// @brief メンバー変数とJSON項目を対応付ける。
export template <typename MemberPtr, typename Converter, typename OmittedBehavior>
struct JsonField {
    static_assert(std::is_member_object_pointer_v<MemberPtr>,
        "JsonField requires MemberPtr to be a member object pointer");
    using Traits = MemberPointerTraits<MemberPtr>; 
    using Owner = typename Traits::Owner;
    using Value = typename Traits::Value;
    static_assert(IsJsonConverter<Converter, MemberPointerValueType<MemberPtr>>,
        "Converter must satisfy IsJsonConverter for the member value type");
    static_assert(IsJsonFieldOmittedBehavior<OmittedBehavior, Value>,
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

    /// @brief JSON から値を読み取り、所有者のメンバに設定する。
    /// @param parser 読み取り元の JsonParser
    /// @param owner 代入先の所有者
    void read(JsonParser& parser, Owner& owner) const {
        owner.*member = converter_.get().read(parser);
    }

    /// @brief JSON項目（キーと値）を書き出す。
    /// @param writer 書き込み先の JsonWriter
    /// @param owner 書き出し元の所有者
    void write(JsonWriter& writer, const Owner& owner) const {
        const auto& value = owner.*member;
        if (omittedBehavior_.shouldSkipWrite(value)) {
            return;
        }
        writer.key(key);
        converter_.get().write(writer, value);
    }

    /// @brief 欠落時の挙動を適用する。
    /// @param owner 欠落時に代入する対象の所有者
    void applyMissing(Owner& owner) const {
        omittedBehavior_.applyMissing(owner.*member, key);
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

/// @brief 読み込み時省略では何も行わず、書き込み時も省略しないJsonFieldを返す。
///        与えられた MemberPtr の値型（以下）に対するコンバータを使用する。
///        基本型、HasJsonFields、HasReadJson/HasWriteJson
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
export template <typename MemberPtr>
constexpr auto getInitialOmittedField(MemberPtr memberPtr, const char* keyName) {
    using Value = MemberPointerValueType<MemberPtr>;
    const auto& converter = getConverter<Value>();
    NoDefaultFieldOmitBehavior<Value> behavior{};
    return getField(memberPtr, keyName, converter, behavior);
}

/// @brief 読み込み時省略では何も行わず、書き込み時も省略しないJsonFieldを返す。
/// @param memberPtr メンバポインタ
/// @param keyName JSONキー名
/// @param converter 値型に対応するコンバータ
export template <typename MemberPtr, typename Converter>
    requires IsJsonConverter<Converter, MemberPointerValueType<MemberPtr>>
constexpr auto getInitialOmittedField(MemberPtr memberPtr, const char* keyName,
    const Converter& converter) {
    using Value = MemberPointerValueType<MemberPtr>;
    NoDefaultFieldOmitBehavior<Value> behavior{};
    return getField(memberPtr, keyName, converter, behavior);
}

}  // namespace rai::serialization
