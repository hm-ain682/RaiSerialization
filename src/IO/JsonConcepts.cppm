module;
#include <type_traits>
#include <concepts>
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
// json入出力対象型は以下の通り。
// ・プリミティブ型（int, double, bool など）
// ・std::string
// ・vector, unique_ptr, variant；要素がjson入出力対象型であること。
// ・jsonFields()を持つ型→HasJsonFields

/// @brief プリミティブ型かどうかを判定するconcept。
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

// ******************************************************************************** メタタイプ & ユーティリティ（追加）

/// @brief ポインタ型から要素型を抽出するメタ関数。
/// @tparam T ポインタ型（unique_ptr<T>、shared_ptr<T>、T*）。
template <typename T>
struct PointerElementType;

template <typename T>
struct PointerElementType<std::unique_ptr<T>> {
    using type = T;
};

template <typename T>
struct PointerElementType<std::shared_ptr<T>> {
    using type = T;
};

template <typename T>
struct PointerElementType<T*> {
    using type = T;
};

/// @brief std::vector型を判定するメタ関数。
template <typename T>
struct IsStdVector : std::false_type {};

template <typename U, typename Alloc>
struct IsStdVector<std::vector<U, Alloc>> : std::true_type {};

/// @brief std::variant型を判定するメタ関数。
template <typename T>
struct IsStdVariant : std::false_type {};

template <typename... Types>
struct IsStdVariant<std::variant<Types...>> : std::true_type {};

/// @brief std::unique_ptr型を判定するconcept。
template <typename T>
concept UniquePointer = std::is_same_v<std::remove_cvref_t<T>,
    std::unique_ptr<typename PointerElementType<std::remove_cvref_t<T>>::type>>;

/// @brief 文字列系型かどうかを判定するconcept。
template <typename T>
concept StringLike = std::is_same_v<std::remove_cvref_t<T>, std::string> ||
    std::is_same_v<std::remove_cvref_t<T>, std::string_view>;

/// @brief 常にfalseを返す補助変数テンプレート。
template <typename>
inline constexpr bool AlwaysFalse = false;

/// @brief ポインタ型（unique_ptr/shared_ptr/生ポインタ）であることを確認するconcept。
template <typename T>
concept SmartOrRawPointer = requires {
    typename PointerElementType<T>::type;
} && (std::is_same_v<T, std::unique_ptr<typename PointerElementType<T>::type>> ||
      std::is_same_v<T, std::shared_ptr<typename PointerElementType<T>::type>> ||
      std::is_same_v<T, typename PointerElementType<T>::type*>);

/// @brief ポインタ型のvectorであることを確認するconcept。
template <typename T>
concept VectorOfPointers = requires {
    typename T::value_type;
} && SmartOrRawPointer<typename T::value_type> &&
     (std::is_same_v<T, std::vector<typename T::value_type>>);

}