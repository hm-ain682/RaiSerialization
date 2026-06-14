module;

#include <gtest/gtest.h>
#include <string>

export module rai.serialization.test_helper;

import rai.serialization.json_io;
import rai.serialization.core;

export namespace rai::serialization::test {

/// @brief Converter を使ってオブジェクトの JSON ラウンドトリップを検証する。
/// @tparam Converter 変換器型。
/// @param original 元のオブジェクト。
/// @param expectedJson 期待される JSON 文字列。
/// @param converter 値変換器。
/// @param context FormatReader に持たせる呼び出し側定義の任意のオブジェクト。省略可。
template <typename Converter, typename... Context>
    requires IsObjectConverter<Converter, typename Converter::Value>
    && (sizeof...(Context) <= 1)
void testJsonRoundTrip(const typename Converter::Value& original,
    const std::string& expectedJson, const Converter& converter, Context&... context) {
    // JSON形式で書き出す
    auto json = getJsonContent(original, converter);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, expectedJson);

    // JSONから読み込む
    typename Converter::Value parsed;
    readJsonString(json, parsed, converter, context...);

    // 元のオブジェクトと内容が一致していることを確認
    if constexpr (requires(const typename Converter::Value& a, const typename Converter::Value& b) {
        { a == b } -> std::convertible_to<bool>;
    }) {
        EXPECT_TRUE(parsed == original);
    }
    else {
        EXPECT_TRUE(parsed.equals(original));
    }
}

/// @brief オブジェクトを JSON へ書き出し、読み戻した値が元と一致することを検証する。
/// @tparam T テスト対象の型。
/// @param original 元のオブジェクト。
/// @param expectedJson 期待される JSON 文字列。
template <typename T>
    requires (HasSerializer<T> || HasReadFormat<T>)
void testJsonRoundTrip(const T& original, const std::string& expectedJson) {
    testJsonRoundTrip(original, expectedJson, getConverter<T>());
}

} // namespace rai::serialization::test
