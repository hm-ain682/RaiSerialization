module;

#include <gtest/gtest.h>
#include <string>

export module rai.json.test_helper;

import rai.json.json_io;
import rai.json.json_concepts;

export namespace rai::json::test {

/// @brief オブジェクトをJSON形式で書き出し、仕様との一致を確認し、読み込んで元と比較する。
/// @tparam T テスト対象の型。HasJsonFieldsを満たす必要がある。
/// @param original 元のオブジェクト。
/// @param expectedJson 期待されるJSON文字列。
/// @note この関数は以下の手順を実行する：
///       1. JSON形式で文字列に書き出す
///       2. JSONの内容が仕様にあっていることを確認
///       3. そのJSONを読み込んでオブジェクトを構築
///       4. 元のオブジェクトと内容が一致していることを確認
template <typename T>
    requires HasJsonFields<T> &&
             requires(const T& a, const T& b) { { a == b } -> std::convertible_to<bool>; }
void testJsonRoundTrip(const T& original, const std::string& expectedJson) {
    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, expectedJson);

    // JSONから読み込む
    T parsed;
    readJsonString(json, parsed);

    // 元のオブジェクトと内容が一致していることを確認
    EXPECT_EQ(parsed, original);
}

} // namespace rai::json::test
