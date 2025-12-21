// @file JsonWriter.cppm
// @brief JSONライターの定義。構造体からJSON5へのシリアライズを提供する。

module;
#include <cassert>
#include <cctype>
#include <cmath>
#include <ostream>
#include <string_view>
#include <type_traits>

export module rai.json.json_writer;

export namespace rai::json {

// @brief JSON5出力用の簡易Writer。
// JSON5形式でデータを出力する。
// @tparam AllowNonIdentifierKeys 識別子として無効なキーを許容するか（既定値：false）
// - AllowNonIdentifierKeys=false: キーは必ず識別子として有効であることを前提とし、引用符なしで出力（無効なキーはアサート失敗）
// - AllowNonIdentifierKeys=true: 識別子として無効なキーも許容し、その場合は引用符で囲んで出力
template <bool AllowNonIdentifierKeys = false>
class JsonWriterBase {
    std::ostream& stream_;
    bool needsComma_;  // 次の要素の前にカンマが必要かどうか

    // @brief 文字列をエスケープして出力
    // @param str エスケープする文字列
    void escapeString(std::string_view str) {
        // JSON5では単一引用符も使えるが、ここでは二重引用符を使用
        stream_ << '"';
        // 各文字をエスケープ処理
        for (char c : str) {
            // エスケープが必要な文字を処理
            switch (c) {
                // 引用符が"なので'のエスケープは要らない。
                case '"':  stream_ << "\\\""; break;
                case '\\': stream_ << "\\\\"; break;
                case '\b': stream_ << "\\b"; break;
                case '\f': stream_ << "\\f"; break;
                case '\n': stream_ << "\\n"; break;
                case '\r': stream_ << "\\r"; break;
                case '\t': stream_ << "\\t"; break;
                case '\v': stream_ << "\\v"; break;
                case '\0': stream_ << "\\0"; break;
                default:
                    // 制御文字の場合は\uXXXX形式でエスケープ
                    if (static_cast<unsigned char>(c) < 0x20) {
                        stream_ << "\\u";
                        char buf[5];
                        snprintf(buf, sizeof(buf), "%04x", static_cast<unsigned char>(c));
                        stream_ << buf;
                    } else {
                        stream_ << c;
                    }
                    break;
            }
        }
        stream_ << '"';
    }

    // @brief 1つのUTF-16コードユニットを \uXXXX で出力
    void writeUnicodeEscape16(unsigned u) {
        stream_ << "\\u";
        char buf[5];
        snprintf(buf, sizeof(buf), "%04x", (u & 0xFFFFu));
        stream_ << buf;
    }

    // @brief キーが識別子として有効かチェック
    // @param keyName チェックするキー名
    // @return 識別子として有効な場合true
    bool isValidIdentifier(std::string_view keyName) const {
        // 空文字列は識別子として無効
        if (keyName.empty()) {
            return false;
        }

        // 最初の文字は英字、$、_のいずれか
        char first = keyName[0];
        if (!std::isalpha(static_cast<unsigned char>(first)) && first != '$' && first != '_') {
            return false;
        }

        // 2文字目以降は英数字、$、_のいずれか
        for (size_t i = 1; i < keyName.size(); ++i) {
            char c = keyName[i];
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '$' && c != '_') {
                return false;
            }
        }

        return true;
    }

    // @brief カンマ出力の前処理
    // 前の要素があればカンマを出力する
    void writeCommaIfNeeded() {
        // カンマが必要な場合は出力
        if (needsComma_) {
            stream_ << ',';
        }
        needsComma_ = true;
    }

public:
    // @brief コンストラクタ
    // @param os 出力先ストリーム
    JsonWriterBase(std::ostream& os) : stream_(os), needsComma_(false)
    {}

    // @brief オブジェクトの開始
    void startObject() {
        writeCommaIfNeeded();
        stream_ << '{';
        needsComma_ = false;
    }

    // @brief オブジェクトの終了
    void endObject() {
        stream_ << '}';
        needsComma_ = true;
    }

    // @brief 配列の開始
    void startArray() {
        writeCommaIfNeeded();
        stream_ << '[';
        needsComma_ = false;
    }

    // @brief 配列の終了
    void endArray() {
        stream_ << ']';
        needsComma_ = true;
    }

    // @brief キーの書き込み
    // @param keyName キー名
    void key(std::string_view keyName) {
        writeCommaIfNeeded();

        if constexpr (AllowNonIdentifierKeys) {
            // 識別子として無効なキーも許容する場合
            // JSON5では識別子として有効なキーは引用符なしで出力
            if (isValidIdentifier(keyName)) {
                stream_ << keyName;
            } else {
                // 識別子として無効な場合は引用符で囲む
                escapeString(keyName);
            }
        } else {
            // 識別子として無効なキーは許容しない場合
            // キーが識別子として有効であることをアサート
            assert(isValidIdentifier(keyName) && "Key must be a valid identifier");
            stream_ << keyName;
        }
        stream_ << ':';
        needsComma_ = false;
    }

    // @brief null値の書き込み
    void null() {
        writeCommaIfNeeded();
        stream_ << "null";
    }

    // プロパティ名なしでの書き出し（ルート要素やArray要素用）

    // @brief bool値の書き込み
    // @param value 書き込む値
    void writeObject(bool value) {
        writeCommaIfNeeded();
        stream_ << (value ? "true" : "false");
    }

    // @brief 1文字を文字列として書き込み（エスケープ対応）
    // @param value 書き込む値
    void writeObject(char value) {
        writeCommaIfNeeded();
        char c = value;
        escapeString(std::string_view(&c, 1));
    }

    // @brief 1バイト文字（符号付き）を文字列として書き込み
    // @param value 書き込む値
    void writeObject(signed char value) {
        writeObject(static_cast<char>(value));
    }

    // @brief 1バイト文字（符号なし）を文字列として書き込み
    // @param value 書き込む値
    void writeObject(unsigned char value) {
        writeObject(static_cast<char>(value));
    }

    // @brief UTF-8コードユニット（1バイト）を1文字の文字列として出力
    void writeObject(char8_t value) {
        writeCommaIfNeeded();
        unsigned byte = static_cast<unsigned>(value);
        if (byte < 0x80) {
            char c = static_cast<char>(byte);
            escapeString(std::string_view(&c, 1));
        } else {
            stream_ << '"';
            // 非ASCIIの単一バイトは \u00XX で表現
            writeUnicodeEscape16(0x00u | byte);
            stream_ << '"';
        }
    }

    // @brief UTF-16コードユニットを1文字の文字列として出力（サロゲートもそのままコードユニットとして出力）
    void writeObject(char16_t value) {
        writeCommaIfNeeded();
        stream_ << '"';
        writeUnicodeEscape16(static_cast<unsigned>(value));
        stream_ << '"';
    }

    // @brief ワイド文字を1文字の文字列として出力
    void writeObject(wchar_t value) {
        if constexpr (sizeof(wchar_t) == 2) {
            writeObject(static_cast<char16_t>(value));
        } else {
            writeObject(static_cast<char32_t>(value));
        }
    }

    // @brief Unicodeスカラー値を1文字の文字列として出力
    void writeObject(char32_t value) {
        writeCommaIfNeeded();
        unsigned cp = static_cast<unsigned>(value);
        if (cp < 0x80u) {
            char c = static_cast<char>(cp);
            escapeString(std::string_view(&c, 1));
            return;
        }
        stream_ << '"';
        if (cp <= 0xFFFFu) {
            writeUnicodeEscape16(cp);
        } else if (cp <= 0x10FFFFu) {
            unsigned v = cp - 0x10000u;
            unsigned high = 0xD800u + (v >> 10);
            unsigned low  = 0xDC00u + (v & 0x3FFu);
            writeUnicodeEscape16(high);
            writeUnicodeEscape16(low);
        } else {
            // 範囲外はU+FFFDにフォールバック
            writeUnicodeEscape16(0xFFFDu);
        }
        stream_ << '"';
    }

    // @brief 整数値の書き込み
    // @param value 書き込む値（すべての整数プリミティブ型に対応）
    template<typename T>
        requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
    void writeObject(T value) {
        writeCommaIfNeeded();
        stream_ << value;
    }

    // @brief 浮動小数点数値の書き込み
    // @param value 書き込む値（すべての浮動小数点数プリミティブ型に対応）
    template<typename T>
        requires std::is_floating_point_v<T>
    void writeObject(T value) {
        writeCommaIfNeeded();

        // 特殊な値のチェック
        if (std::isnan(value)) {
            stream_ << "NaN";
        } else if (std::isinf(value)) {
            // JSON5では正負の無限大をサポート
            if (value > 0) {
                stream_ << "Infinity";
            } else {
                stream_ << "-Infinity";
            }
        } else {
            stream_ << value;
        }
    }

    // @brief 文字列値の書き込み
    // @param value 書き込む値
    void writeObject(std::string_view value) {
        writeCommaIfNeeded();
        escapeString(value);
    }
};

// @brief JsonWriterの既定型エイリアス（識別子のみ許可）
using JsonWriter = JsonWriterBase<false>;

}  // namespace rai::json
