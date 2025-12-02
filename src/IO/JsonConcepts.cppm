module;
#include <type_traits>
#include <concepts>

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

}