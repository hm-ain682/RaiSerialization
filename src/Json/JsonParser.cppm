// @file JsonParser.cppm
// @brief JSON5パーサーの定義。トークン列からオブジェクトを構築する。

module;
#include <cassert>
#include <stdexcept>
#include <string>
#include <string_view>
#ifndef NDEBUG
#include <typeindex>
#endif
#include <type_traits>
#include <variant>

export module rai.serialization.json:json_parser;

import rai.serialization.token_manager;

export namespace rai::serialization {

// @brief トークン管理型が満たすべきインターフェース

// ******************************************************************************** JsonParser
// @brief JSON5パーサー（トークン列からオブジェクトを構築）
class FormatContext {
public:
    FormatContext() = default;

    FormatContext(const FormatContext&) = default;
    FormatContext& operator=(const FormatContext&) = default;

    template <typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, FormatContext>)
    explicit FormatContext(T& value)
        : ptr_(&value)
#ifndef NDEBUG
        , type_(typeid(std::remove_cvref_t<T>))
#endif
    {}

    template <typename T>
    T* get() const {
#ifndef NDEBUG
        assert(type_ == std::type_index(typeid(std::remove_cvref_t<T>)));
#endif
        return static_cast<T*>(ptr_);
    }

private:
    void* ptr_{};
#ifndef NDEBUG
    std::type_index type_{typeid(void)};
#endif
};

/// @brief フォーマット読み込み中に検出した、読み取り継続可能な問題の通知先。
/// @details JSON 固有ではなく、未知キーや値の型不一致など、上位層が警告・無視・例外化を
///          選べる問題を扱う。デフォルト実装は未知キーを無視し、回復可能エラーは継続しない。
class FormatIssueSink {
public:
    virtual ~FormatIssueSink() = default;

    /// @brief 入力に存在したが、読み込み対象の型に対応するフィールドがないキーを通知する。
    /// @param key 未知キー名。
    /// @param position 未知キーに対応する値トークンの入力位置。
    virtual void unknownKey(std::string_view key, std::size_t position) {
        (void)key;
        (void)position;
    }

    /// @brief 呼び出し側が回復可能と判断した読み取りエラーを通知する。
    /// @param message エラー内容。
    /// @param position 問題が起きたトークンの入力位置。
    /// @return true の場合は呼び出し側の定めた方法で読み取りを継続し、false の場合は例外を送出する。
    virtual bool recoverableError(std::string_view message, std::size_t position) {
        (void)message;
        (void)position;
        return false;
    }
};

/// @brief 問題を無視し、回復可能エラーでは例外を送出させる既定の Sink を返す。
inline FormatIssueSink& defaultFormatIssueSink() {
    static FormatIssueSink sink{};
    return sink;
}

class JsonParser {
    // ******************************************************************************** トークン取得
private:
    // 次のトークンを取得して消費
    // @note generateAllTokens()で必ずEndOfStreamTagが追加されるため、トークンは常に存在する
    JsonToken take() { return tokenManager_.take(); }

    // 次のトークンを取得（消費しない）
    // @note generateAllTokens()で必ずEndOfStreamTagが追加されるため、トークンは常に存在する
    const JsonToken& peekToken() const { return tokenManager_.peek(); }

    // ******************************************************************************** 構築
public:
    // @brief コンストラクタ（トークン管理オブジェクトを指定）
    // @param tokenManager トークン管理オブジェクトの参照
    // @param context Converter から参照する呼び出し側所有の任意データ。
    // @param issueSink 未知キーや回復可能エラーの通知先。
    explicit JsonParser(TokenManager& tokenManager, FormatContext context = {},
        FormatIssueSink& issueSink = defaultFormatIssueSink())
        : tokenManager_(tokenManager), context_(context), issueSink_(issueSink) {}

    template <typename T>
    T* context() const {
        return context_.get<T>();
    }

    // ******************************************************************************** トークン読み取り
public:
    // @brief 次のトークンの開始位置を返す。
    // @return 次のトークンの入力ストリーム内での開始位置
    std::size_t nextPosition() const {
        return peekToken().position;
    }

    // @brief 次のトークンの種類を返す。
    // @return 次のトークンの種類を示すJsonTokenType値
    JsonTokenType nextTokenType() const {
        // JsonTokenValueとJsonTokenTypeの列挙値の順序が一致していることを前提とする
        return static_cast<JsonTokenType>(peekToken().value.index());
    }

    // 構造トークン
    std::size_t startObject() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::StartObjectTag>(t.value)) {
            typeError("object start '{'");
        }
        return t.position;
    }

    std::size_t endObject() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::EndObjectTag>(t.value)) {
            typeError("object end '}'");
        }
        return t.position;
    }

    std::size_t startArray() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::StartArrayTag>(t.value)) {
            typeError("array start '['");
        }
        return t.position;
    }

    std::size_t endArray() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::EndArrayTag>(t.value)) {
            typeError("array end ']'");
        }
        return t.position;
    }

    // 次が EndArray / EndObject か確認（消費しない）
    // @note peekToken()は常に成功する（EndOfStreamTagが保証されている）
    bool nextIsEndArray() {
        return std::holds_alternative<json_token_detail::EndArrayTag>(peekToken().value);
    }

    bool nextIsEndObject() {
        return std::holds_alternative<json_token_detail::EndObjectTag>(peekToken().value);
    }

    // 次がnullならtrueを返す。それ以外のトークンならfalseを返す。
    // @note peekToken()は常に成功する（EndOfStreamTagが保証されている）
    bool nextIsNull() {
        return std::holds_alternative<json_token_detail::NullTag>(peekToken().value);
    }

    // キー
    std::string nextKey() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::KeyVal>(t.value)) {
            typeError("object key");
        }
        return std::get<json_token_detail::KeyVal>(t.value).v;
    }

    void expectKey(const char* expected) {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::KeyVal>(t.value)) {
            typeError("object key");
        }
        if (std::get<json_token_detail::KeyVal>(t.value).v != expected) {
            throw std::runtime_error(
                std::string("JsonParser: unexpected key '") +
                std::get<json_token_detail::KeyVal>(t.value).v + "', expected '" + std::string(expected) + "'");
        }
    }

    // 値読み取り
    void readTo(bool& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::BoolVal>(t.value)) {
            out = static_cast<bool>(std::get<json_token_detail::BoolVal>(take().value).v);
            return;
        }
        recoverableValueError("JsonParser: expected bool");
    }

    template<typename T>
        requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
    void readTo(T& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::IntVal>(t.value)) {
            out = static_cast<T>(std::get<json_token_detail::IntVal>(take().value).v);
            return;
        }
        recoverableValueError("JsonParser: expected integer");
    }

    template<typename T>
        requires std::is_floating_point_v<T>
    void readTo(T& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::NumVal>(t.value)) {
            out = static_cast<T>(std::get<json_token_detail::NumVal>(take().value).v);
            return;
        }
        if (std::holds_alternative<json_token_detail::IntVal>(t.value)) {
            out = static_cast<T>(std::get<json_token_detail::IntVal>(take().value).v);
            return;
        }
        recoverableValueError("JsonParser: expected number");
    }

    void readTo(std::string& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::StrVal>(t.value)) {
            out = std::get<std::string>(take().value);
            return;
        }
        recoverableValueError("JsonParser: expected string");
    }

    // @brief 文字列から1文字を読み込む
    void readTo(char& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::StrVal>(t.value)) {
            const auto position = t.position;
            auto str = std::get<json_token_detail::StrVal>(take().value);
            if (str.size() == 1) {
                out = str[0];
                return;
            }
            recoverableError("JsonParser: expected single character string", position);
            return;
        }
        recoverableValueError("JsonParser: expected string");
    }

    // @brief 文字列から1文字を読み込む（符号付き）
    void readTo(signed char& out) {
        char temp;
        readTo(temp);
        out = static_cast<signed char>(temp);
    }

    // @brief 文字列から1文字を読み込む（符号なし）
    void readTo(unsigned char& out) {
        char temp;
        readTo(temp);
        out = static_cast<unsigned char>(temp);
    }

    // @brief 文字列から1バイトを読み込む（UTF-8コードユニット）
    void readTo(char8_t& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::StrVal>(t.value)) {
            const auto position = t.position;
            auto str = std::get<json_token_detail::StrVal>(take().value);
            if (str.size() == 1) {
                out = static_cast<char8_t>(static_cast<unsigned char>(str[0]));
                return;
            }
            recoverableError("JsonParser: expected single byte string for char8_t", position);
            return;
        }
        recoverableValueError("JsonParser: expected string");
    }

    // @brief 文字列から1コードポイントを読み込む（UTF-16）
    void readTo(char16_t& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::StrVal>(t.value)) {
            const auto position = t.position;
            auto str = std::get<json_token_detail::StrVal>(take().value);
            char32_t codePoint;
            size_t byteCount;
            if (!decodeUtf8FirstCodePoint(str, codePoint, byteCount)) {
                recoverableError("JsonParser: invalid UTF-8 sequence for char16_t", position);
                return;
            }
            // UTF-8文字列全体が1コードポイントであることを確認
            if (byteCount != str.size()) {
                recoverableError("JsonParser: char16_t requires single code point (multi-character string given)", position);
                return;
            }
            // BMP範囲のみサポート（入れる先が1要素しかないのでサロゲートペアには対応できない）
            if (codePoint > 0xFFFF) {
                recoverableError("JsonParser: char16_t does not support code points beyond BMP (U+FFFF)", position);
                return;
            }

            out = static_cast<char16_t>(codePoint);
            return;
        }
        recoverableValueError("JsonParser: expected string");
    }

    // @brief 文字列から1コードポイントを読み込む（UTF-32）
    void readTo(char32_t& out) {
        const auto& t = peekToken();
        if (std::holds_alternative<json_token_detail::StrVal>(t.value)) {
            const auto position = t.position;
            auto str = std::get<json_token_detail::StrVal>(take().value);
            char32_t codePoint;
            size_t byteCount;

            if (!decodeUtf8FirstCodePoint(str, codePoint, byteCount)) {
                recoverableError("JsonParser: invalid UTF-8 sequence for char32_t", position);
                return;
            }

            // UTF-8文字列全体が1コードポイントであることを確認
            if (byteCount != str.size()) {
                recoverableError("JsonParser: char32_t requires single code point (multi-character string given)", position);
                return;
            }

            out = codePoint;
            return;
        }
        recoverableValueError("JsonParser: expected string");
    }

    // @brief 文字列から1文字を読み込む（ワイド文字）
    void readTo(wchar_t& out) {
        if constexpr (sizeof(wchar_t) == 2) {
            char16_t temp;
            readTo(temp);
            out = static_cast<wchar_t>(temp);
        } else {
            char32_t temp;
            readTo(temp);
            out = static_cast<wchar_t>(temp);
        }
    }

private:
    // @brief UTF-8継続バイト（10xxxxxx）をチェックして下位6ビットを抽出
    // @return 有効な継続バイトならそのビット値（0-63）、無効なら-1
    static int decodeUtf8ContinuationByte(unsigned char byte) {
        if ((byte & 0xC0) != 0x80) {
            return -1;
        }
        return byte & 0x3F;
    }

    // @brief UTF-8文字列の先頭から1コードポイントをデコード
    // @param str UTF-8エンコード文字列
    // @param outCodePoint デコードされたコードポイント
    // @param outByteCount 消費したバイト数
    // @return デコード成功時true
    static bool decodeUtf8FirstCodePoint(std::string_view str, char32_t& outCodePoint, size_t& outByteCount) {
        if (str.empty()) {
            return false;
        }

        unsigned char byte0 = static_cast<unsigned char>(str[0]);

        // 1バイト文字 (0xxxxxxx)
        if (byte0 < 0x80) {
            outCodePoint = static_cast<char32_t>(byte0);
            outByteCount = 1;
            return true;
        }

        // 2バイト文字 (110xxxxx 10xxxxxx)
        if ((byte0 & 0xE0) == 0xC0) {
            if (str.size() < 2) {
                return false;
            }
            int bits1 = decodeUtf8ContinuationByte(static_cast<unsigned char>(str[1]));
            if (bits1 < 0) {
                return false;
            }

            outCodePoint = ((byte0 & 0x1F) << 6) | bits1;
            outByteCount = 2;
            return outCodePoint >= 0x80; // オーバーロングチェック
        }

        // 3バイト文字 (1110xxxx 10xxxxxx 10xxxxxx)
        if ((byte0 & 0xF0) == 0xE0) {
            if (str.size() < 3) {
                return false;
            }
            int bits1 = decodeUtf8ContinuationByte(static_cast<unsigned char>(str[1]));
            int bits2 = decodeUtf8ContinuationByte(static_cast<unsigned char>(str[2]));
            if (bits1 < 0 || bits2 < 0) {
                return false;
            }

            outCodePoint = ((byte0 & 0x0F) << 12) | (bits1 << 6) | bits2;
            outByteCount = 3;
            return outCodePoint >= 0x800 && (outCodePoint < 0xD800 || outCodePoint > 0xDFFF); // オーバーロング&サロゲートチェック
        }

        // 4バイト文字 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if ((byte0 & 0xF8) == 0xF0) {
            if (str.size() < 4) {
                return false;
            }
            int bits1 = decodeUtf8ContinuationByte(static_cast<unsigned char>(str[1]));
            int bits2 = decodeUtf8ContinuationByte(static_cast<unsigned char>(str[2]));
            int bits3 = decodeUtf8ContinuationByte(static_cast<unsigned char>(str[3]));
            if (bits1 < 0 || bits2 < 0 || bits3 < 0) {
                return false;
            }

            outCodePoint = ((byte0 & 0x07) << 18) | (bits1 << 12) | (bits2 << 6) | bits3;
            outByteCount = 4;
            return outCodePoint >= 0x10000 && outCodePoint <= 0x10FFFF; // 範囲チェック
        }

        return false; // 不正なUTF-8
    }

public:
    // 値全体をスキップ（未知キーなどで使用）。プリミティブ/配列/オブジェクトを丸ごと消費する。
    // 未知キーを Sink に通知する。値自体の消費は呼び出し側が skipValue() で行う。
    void noteUnknownKey(std::string_view key) {
        issueSink_.unknownKey(key, nextPosition());
    }

    // 値全体をスキップする。未知キーなど、値を構築しない場合に使用する。
    // プリミティブは1トークン、配列とオブジェクトは対応する終端まで丸ごと消費する。
    void skipValue() {
        auto t = take();
        // プリミティブ/Null/文字列/数値/真偽は1トークンで完結
        if (std::holds_alternative<json_token_detail::NullTag>(t.value) ||
            std::holds_alternative<json_token_detail::BoolVal>(t.value) ||
            std::holds_alternative<json_token_detail::IntVal>(t.value) ||
            std::holds_alternative<json_token_detail::NumVal>(t.value) ||
            std::holds_alternative<json_token_detail::StrVal>(t.value)) {
            return;
        }
        // オブジェクト: { key: value, ... }
        if (std::holds_alternative<json_token_detail::StartObjectTag>(t.value)) {
            while (!nextIsEndObject()) {
                (void)nextKey();  // キーを消費
                skipValue();      // 対応する値をスキップ
            }
            endObject();
            return;
        }
        // 配列: [ v1, v2, ... ]
        if (std::holds_alternative<json_token_detail::StartArrayTag>(t.value)) {
            while (!nextIsEndArray()) {
                skipValue();
            }
            endArray();
            return;
        }
        if (std::holds_alternative<json_token_detail::EndOfStreamTag>(t.value)) {
            typeError("value (got end-of-stream)");
        }
        if (std::holds_alternative<json_token_detail::KeyVal>(t.value)) {
            typeError("value (got key)");
        }
    }

private:
    // Sink が継続を選ばなかった場合は、従来通り例外として扱う。
    void recoverableError(std::string_view message, std::size_t position) {
        if (!issueSink_.recoverableError(message, position)) {
            throw std::runtime_error(std::string(message));
        }
    }

    // 現在位置の値を構築できないことを Sink に通知する。
    // readTo の型不一致では、継続時に現在位置の値全体を skipValue() で読み飛ばす。
    void recoverableValueError(std::string_view message) {
        recoverableError(message, nextPosition());
        skipValue();
    }

    [[noreturn]] static void typeError(const char* expected) {
        throw std::runtime_error(std::string("JsonParser: expected ") + expected);
    }

    // ******************************************************************************** メンバー変数
private:
    TokenManager& tokenManager_;       ///< トークン管理オブジェクトの参照
    FormatContext context_{};
    FormatIssueSink& issueSink_;
};

}  // namespace rai::serialization
