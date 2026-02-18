// @file TokenManager.cppm
// @brief JSONトークンの定義とトークン管理クラス

module;
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <variant>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <utility>

export module rai.serialization.token_manager;

export namespace rai::serialization {

// ******************************************************************************** トークン型定義（内部実装詳細）
namespace json_token_detail {

// @brief 入力ストリーム終端を示すタグ
struct EndOfStreamTag {};

// @brief null値を示すタグ
struct NullTag {};

// @brief オブジェクト開始を示すタグ
struct StartObjectTag {};

// @brief オブジェクト終了を示すタグ
struct EndObjectTag {};

// @brief 配列開始を示すタグ
struct StartArrayTag {};

// @brief 配列終了を示すタグ
struct EndArrayTag {};

// @brief 真偽値を保持する型
struct BoolVal {
    bool v{};
};

// @brief 整数値を保持する型
struct IntVal {
    std::int64_t v{};
};

// @brief 浮動小数点数値を保持する型
struct NumVal {
    double v{};
};

// @brief キー名を保持する型
struct KeyVal {
    std::string v;
};

// @brief 文字列値を保持する型（キーとは区別）
using StrVal = std::string;

}  // namespace json_token_detail

// @brief JSONトークンの種類を表す列挙型
enum class JsonTokenType {
    EndOfStream,    ///< 入力ストリーム終端
    Null,           ///< null値
    Bool,           ///< 真偽値
    Integer,        ///< 整数値
    Number,         ///< 浮動小数点数値
    String,         ///< 文字列値
    Key,            ///< キー名
    StartObject,    ///< オブジェクト開始
    EndObject,      ///< オブジェクト終了
    StartArray,     ///< 配列開始
    EndArray        ///< 配列終了
};

// @brief JSONトークンのvariant表現
// @note 内部実装の詳細型はjson_token_detail名前空間に隠蔽されている
using JsonTokenValue =
    std::variant<json_token_detail::EndOfStreamTag, json_token_detail::NullTag,
                 json_token_detail::BoolVal, json_token_detail::IntVal, json_token_detail::NumVal,
                 json_token_detail::StrVal, json_token_detail::KeyVal,
                 json_token_detail::StartObjectTag, json_token_detail::EndObjectTag,
                 json_token_detail::StartArrayTag, json_token_detail::EndArrayTag>;

// @brief JSONトークン（値と入力位置を保持）
struct JsonToken {
    JsonTokenValue value{};    ///< トークンの種類と値
    std::size_t position{};    ///< 入力ストリーム内での開始位置

    JsonToken() = default;
    JsonToken(const JsonToken&) = default;
    JsonToken(JsonToken&&) = default;
    JsonToken& operator=(const JsonToken&) = default;
    JsonToken& operator=(JsonToken&&) = default;

    JsonToken(JsonTokenValue v, std::size_t pos)
        : value(std::move(v)), position(pos) {}

    template <typename T>
    JsonToken(T&& v, std::size_t pos)
        : value(std::forward<T>(v)), position(pos) {}
};

// ******************************************************************************** デフォルトのトークン管理クラス
// @brief dequeを使用したトークン管理クラス
// @note 先頭要素のpopがO(1)で効率的
class TokenManager {
public:
    // @brief トークンを追加
    // @param token 追加するトークン
    void pushToken(JsonToken&& token) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (error_) {
                return;
            }
            tokens_.push_back(std::move(token));
        }
        condition_.notify_one();
    }

    // @brief 次のトークンを取得して消費
    // @return 取得したトークン
    // @note generateAllTokens()で必ずEndOfStreamTagが追加されるため、tokens_は常に空でない
    JsonToken take() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [&] { return !tokens_.empty() || error_; });
        if (error_ && tokens_.empty()) {
            std::rethrow_exception(error_);
        }
        JsonToken t = std::move(tokens_.front());
        tokens_.pop_front();
        return t;
    }

    // @brief 次のトークンを取得（消費しない）
    // @return 次のトークン
    // @note generateAllTokens()で必ずEndOfStreamTagが追加されるため、tokens_は常に空でない
    const JsonToken& peek() const {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [&] { return !tokens_.empty() || error_; });
        if (error_ && tokens_.empty()) {
            std::rethrow_exception(error_);
        }
        return tokens_.front();
    }

    /// @brief トークナイザー側のエラーを通知する。
    /// @param error 捕捉した例外。
    /// @note パーサー側の待機を解除して例外を再送出させるために使用する。
    void signalError(std::exception_ptr error) {
        if (!error) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) {
                error_ = std::move(error);
                tokens_.clear();
            }
        }
        condition_.notify_all();
    }

private:
    mutable std::mutex mutex_;  ///< トークン列を保護するミューテックス
    mutable std::condition_variable condition_;  ///< トークン到着待ち用の条件変数
    std::exception_ptr error_;  ///< トークナイザーから伝播した例外
    std::deque<JsonToken> tokens_;  ///< トークン列（dequeで先頭popをO(1)に）
};

}  // namespace rai::serialization
