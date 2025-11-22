// @file JsonParser.cppm
// @brief JSON5パーサーの定義。トークン列からオブジェクトを構築する。

module;
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

export module rai.json.json_parser;

import rai.json.json_token_manager;

export namespace rai::json {

// @brief トークン管理型が満たすべきインターフェース
template <typename T>
concept TokenManager = requires(T& t, const T& ct, JsonToken token) {
    // 次のトークンを取得して消費
    { t.take() } -> std::same_as<JsonToken>;
    // 次のトークンを取得（消費しない）
    { ct.peek() } -> std::same_as<const JsonToken&>;
};

// ******************************************************************************** JsonParser
// @brief JSON5パーサー（トークン列からオブジェクトを構築）
template <TokenManager TokMgr>
class JsonParser {
    // ******************************************************************************** トークン取得
private:
    // 次のトークンを取得して消費
    // @note generateAllTokens()で必ずEndOfStreamTagが追加されるため、トークンは常に存在する
    JsonToken take() { return tokenManager_.take(); }

    // 次のトークンを取得（消費しない）
    // @note generateAllTokens()で必ずEndOfStreamTagが追加されるため、トークンは常に存在する
    JsonToken peekToken() const { return tokenManager_.peek(); }

    // ******************************************************************************** 構築
public:
    // @brief コンストラクタ（トークン管理オブジェクトを指定）
    // @param tokenManager トークン管理オブジェクトの参照
    explicit JsonParser(TokMgr& tokenManager) : tokenManager_(tokenManager) {}

    // ******************************************************************************** トークン読み取り
public:
    // 構造トークン
    void startObject() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::StartObjectTag>(t))
            typeError("object start '{'");
    }

    void endObject() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::EndObjectTag>(t))
            typeError("object end '}'");
    }

    void startArray() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::StartArrayTag>(t))
            typeError("array start '['");
    }

    void endArray() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::EndArrayTag>(t))
            typeError("array end ']'");
    }

    // 次が EndArray / EndObject か確認（消費しない）
    // @note peekToken()は常に成功する（EndOfStreamTagが保証されている）
    bool nextIsEndArray() {
        return std::holds_alternative<json_token_detail::EndArrayTag>(peekToken());
    }

    bool nextIsEndObject() {
        return std::holds_alternative<json_token_detail::EndObjectTag>(peekToken());
    }

    // 次がnullならtrueを返す。それ以外のトークンならfalseを返す。
    // @note peekToken()は常に成功する（EndOfStreamTagが保証されている）
    bool nextIsNull() {
        return std::holds_alternative<json_token_detail::NullTag>(peekToken());
    }

    // キー
    std::string nextKey() {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::KeyVal>(t))
            typeError("object key");
        return std::get<json_token_detail::KeyVal>(t).v;
    }

    void expectKey(const char* expected) {
        auto t = take();
        if (!std::holds_alternative<json_token_detail::KeyVal>(t))
            typeError("object key");
        if (std::get<json_token_detail::KeyVal>(t).v != expected) {
            throw std::runtime_error(
                std::string("JsonParser: unexpected key '") + std::get<json_token_detail::KeyVal>(t).v + "', expected '" +
                std::string(expected) + "'");
        }
    }

    // 値読み取り
    void readTo(bool& out) {
        auto t = take();
        if (std::holds_alternative<json_token_detail::BoolVal>(t)) {
            out = static_cast<bool>(std::get<json_token_detail::BoolVal>(t).v);
        } else {
            typeError("bool");
        }
    }

    void readTo(int& out) {
        auto t = take();
        if (std::holds_alternative<json_token_detail::IntVal>(t)) {
            out = static_cast<int>(std::get<json_token_detail::IntVal>(t).v);
        } else {
            typeError("int");
        }
    }

    void readTo(double& out) {
        auto t = take();
        if (std::holds_alternative<json_token_detail::NumVal>(t)) {
            out = std::get<json_token_detail::NumVal>(t).v;
            return;
        }
        if (std::holds_alternative<json_token_detail::IntVal>(t)) {
            out = static_cast<double>(std::get<json_token_detail::IntVal>(t).v);
            return;
        }
        typeError("number");
    }

    void readTo(float& out) {
        double temp{};
        readTo(temp);
        out = static_cast<float>(temp);
    }

    void readTo(std::string& out) {
        auto t = take();
        if (std::holds_alternative<json_token_detail::StrVal>(t)) {
            out = std::get<std::string>(t);
            return;
        }
        typeError("string");
    }

    // 値全体をスキップ（未知キーなどで使用）。プリミティブ/配列/オブジェクトを丸ごと消費する。
    void skipValue() {
        auto t = take();
        // プリミティブ/Null/文字列/数値/真偽は1トークンで完結
        if (std::holds_alternative<json_token_detail::NullTag>(t) ||
            std::holds_alternative<json_token_detail::BoolVal>(t) ||
            std::holds_alternative<json_token_detail::IntVal>(t) ||
            std::holds_alternative<json_token_detail::NumVal>(t) ||
            std::holds_alternative<json_token_detail::StrVal>(t)) {
            return;
        }
        // オブジェクト: { key: value, ... }
        if (std::holds_alternative<json_token_detail::StartObjectTag>(t)) {
            while (!nextIsEndObject()) {
                (void)nextKey();  // キーを消費
                skipValue();      // 対応する値をスキップ
            }
            endObject();
            return;
        }
        // 配列: [ v1, v2, ... ]
        if (std::holds_alternative<json_token_detail::StartArrayTag>(t)) {
            while (!nextIsEndArray()) {
                skipValue();
            }
            endArray();
            return;
        }
        // 想定外（Endマーカー/Keyなど値位置では不正）
        if (std::holds_alternative<json_token_detail::EndOfStreamTag>(t)) {
            typeError("value (got end-of-stream)");
        }
        if (std::holds_alternative<json_token_detail::KeyVal>(t)) {
            typeError("value (got key)");
        }
    }

private:
    [[noreturn]] static void typeError(const char* expected) {
        throw std::runtime_error(std::string("JsonParser: expected ") + expected);
    }

    // ******************************************************************************** メンバー変数
private:
    TokMgr& tokenManager_;       ///< トークン管理オブジェクトの参照
    std::vector<std::string> unknownKeys_{};  ///< 未知キー記録（診断用）

public:
    // @brief 未知キーの一覧を取得して所有権を移動
    std::vector<std::string>&& getUnknownKeys() { return std::move(unknownKeys_); }

    // @brief 未知キーを記録する（後で診断に利用）
    // @param key 未知のキー名
    void noteUnknownKey(std::string key) { unknownKeys_.push_back(std::move(key)); }

    // @brief 未知キーの一覧を取得（const参照）
    const std::vector<std::string>& unknownKeys() const { return unknownKeys_; }
};

}  // namespace rai::json
