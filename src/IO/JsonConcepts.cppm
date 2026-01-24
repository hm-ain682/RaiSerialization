module;
#include <type_traits>
#include <concepts>
#include <ranges>
#include <memory>
#include <vector>
#include <variant>
#include <string>
#include <string_view>

import rai.json.json_writer;
import rai.json.json_parser;
export module rai.json.json_concepts;

export namespace rai::json {

// ******************************************************************************** 概念定義

/// @brief プリミティブ型（int, double, bool など）かどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsFundamentalValue = std::is_fundamental_v<T>;

/// @brief jsonFields()メンバー関数を持つかどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept HasJsonFields = requires(const T& t) { t.jsonFields(); };

/// @brief toJsonメンバー関数を持つかどうかを判定するconcept。
/// @tparam Field フィールド型。
/// @tparam ValueType フィールドが管理する値の型。
template <typename Field, typename ValueType>
concept HasToJson = requires(const Field& field, JsonWriter& writer, const ValueType& value) {
    { field.toJson(writer, value) } -> std::same_as<void>;
};

/// @brief fromJsonメンバー関数を持つかどうかを判定するconcept。
/// @tparam Field フィールド型。
/// @tparam ValueType フィールドが管理する値の型。
/// @note 変更: 以前は std::string を受け取っていたが、JsonParser を直接受け取るようにする。
template <typename Field, typename ValueType>
concept HasFromJson = requires(const Field& field, JsonParser& parser) {
    { field.fromJson(parser) } -> std::convertible_to<ValueType>;
};

/// @brief writeJsonメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasWriteJson = requires(const T& obj, JsonWriter& writer) {
    { obj.writeJson(writer) } -> std::same_as<void>;
};

/// @brief readJsonメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasReadJson = requires(T& obj, JsonParser& parser) {
    { obj.readJson(parser) } -> std::same_as<void>;
};

/// @brief std::vector 型かどうかを判定する concept（allocator を含め正確に判定）。
template <typename T>
concept IsStdVector = requires {
    typename T::value_type;
    typename T::allocator_type;
} && std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>>;

/// @brief std::variant 型かどうかを判定する concept（std::variant 固有の trait を確認）。
template <typename T>
concept IsStdVariant = requires {
    typename std::variant_size<T>::type;
};

/// @brief 文字列系型かどうかを判定するconcept（名前を `LikesString` に変更）。
template <typename T>
concept LikesString = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>;

/// @brief string 系を除くレンジ（配列/コンテナ）を表す concept（名前を `IsContainer` に変更）。
/// @details std::ranges::range を満たし、かつ `LikesString` を除外することで
///          `std::string` を配列として誤判定しないようにします。
template<typename T>
concept IsContainer = std::ranges::range<T> && !LikesString<T>;

/// @brief 常にfalseを返す補助変数テンプレート。
template <typename>
inline constexpr bool AlwaysFalse = false;

/// @brief std::unique_ptr を判定する concept（element_type / deleter_type を確認し正確に判定）。
template <typename T>
concept IsUniquePtr = requires {
    typename T::element_type;
    typename T::deleter_type;
} && std::is_same_v<T, std::unique_ptr<typename T::element_type, typename T::deleter_type>>;

/// @brief std::shared_ptr を判定する concept（element_type を確認し正確に判定）。
template <typename T>
concept IsSharedPtr = requires {
    typename T::element_type;
} && std::is_same_v<T, std::shared_ptr<typename T::element_type>>;

/// @brief ポインタ型（unique_ptr/shared_ptr/生ポインタ）であることを確認する concept（名前を `IsSmartOrRawPointer` に変更）。
template <typename T>
concept IsSmartOrRawPointer = IsUniquePtr<T> || IsSharedPtr<T> || std::is_pointer_v<T>;

/// @brief ポインタ型のvectorであることを確認するconcept（名前を `IsVectorOfPointers` に変更）。
template <typename T>
concept IsVectorOfPointers = requires {
    typename T::value_type;
} && IsSmartOrRawPointer<typename T::value_type> &&
     std::is_same_v<T, std::vector<typename T::value_type>>;

}