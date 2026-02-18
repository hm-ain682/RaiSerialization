// @file JsonTokenizer.cppm
// @brief JSON5トークナイザーの定義。入力文字列からトークン列を生成する。

module;
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

// マクロ定義（識別子文字の列挙）
#define CASE_ALPHA_LOWER \
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': \
    case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': \
    case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z'

#define CASE_ALPHA_UPPER \
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': \
    case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R': \
    case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z'

#define CASE_PART_DIGITS19 \
    case '1': case '2': case '3': case '4': case '5': \
    case '6': case '7': case '8': case '9'
#define CASE_PART_DIGITS case '0': CASE_PART_DIGITS19

export module rai.serialization.json_tokenizer;

import rai.serialization.token_manager;

export namespace rai::serialization {

// 入力文字列取得元のconcept
template <typename T>
concept InputSource = requires(T& t, const T& ct, std::size_t offset, std::size_t count) {
    { ct.peekAhead(offset) } -> std::same_as<char>;
    { t.consume(count) } -> std::same_as<void>;
    { ct.position() } -> std::same_as<std::size_t>;
};

// @brief トークン管理型が満たすべきインターフェース
template <typename T>
concept IsTokenManager = requires(T& t, JsonToken&& token) {
    // トークンを追加。tokenはrvalue referenceだが、
    // 名前付き変数なのでstd::move()でrvalueに変換する必要がある
    { t.pushToken(std::move(token)) } -> std::same_as<void>;
};

// @brief 警告メッセージ出力用のconcept
template <typename T>
concept WarningOutput = requires(T& t, const std::string& msg) {
    { t.warning(msg) } -> std::same_as<void>;
};

// @brief メッセージ出力用の基底クラス。
class MessageOutput {
public:
    /// @brief 警告メッセージを出力する。
    /// @param msg 出力するメッセージ。
    virtual void warning(const std::string& msg) = 0;
};

// @brief 標準出力への警告出力
class StdoutMessageOutput : public MessageOutput {
public:
    /// @brief 警告メッセージを標準出力に出力する。
    /// @param msg 出力するメッセージ。
    void warning(const std::string& msg) {
        // 単純なロギングのみを行う。フォーマットは呼び出し元に依存させる。
        std::cout << "Warning: " << msg << std::endl;
    }
};

// ******************************************************************************** JsonTokenizer
// @brief JSON5トークナイザー（入力文字列からトークン列を生成）
template <InputSource Input, IsTokenManager TokMgr>
class JsonTokenizer {
    // ******************************************************************************** 空白文字とコメントのスキップ
private:
    // @brief 空白文字とコメントをスキップ
    // @note JSON5仕様 Table 3に準拠（ASCII空白 + Unicode Zsカテゴリ + U+2028/U+2029/U+FEFF）
    void skipWhitespaceAndComments() {
        for (;;) {
            char c = peek();
            // 先頭文字でswitch（signed charをunsigned charにキャストして比較）
            switch (static_cast<unsigned char>(c)) {
                // JSON5仕様 8 White Space Table 3
                case ' ':   // U+0020 Space
                case '\t':  // U+0009 Horizontal tab
                case '\n':  // U+000A Line feed
                case '\r':  // U+000D Carriage return
                case '\v':  // U+000B Vertical tab
                case '\f':  // U+000C Form feed
                    consume();
                    continue;
                case 0xC2:  // U+00A0 (Non-breaking space): 0xC2 0xA0
                    if (matchUtf8Bytes(0, 0xC2, 0xA0)) {
                        consume(2);
                        continue;
                    }
                    return;
                case 0xE1:  // U+1680 (Ogham space mark / Zsカテゴリ): 0xE1 0x9A 0x80
                    if (matchUtf8Bytes(1, 0x9A, 0x80)) {
                        consume(3);
                        continue;
                    }
                    return;
                case 0xE2:  // U+2000～U+200A, U+2028, U+2029, U+202F, U+205F
                    switch (static_cast<unsigned char>(peekAhead(1))) {
                    case 0x80:
                        switch (static_cast<unsigned char>(peekAhead(2))) {
                        // U+2000～U+200A (各種スペース / Zsカテゴリ): 0xE2 0x80 0x80～0x8A
                        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84:
                        case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A:
                        case 0xA8: // U+2028 (Line separator): 0xE2 0x80 0xA8
                        case 0xA9: // U+2029 (Paragraph separator): 0xE2 0x80 0xA9
                        case 0xAF: // U+202F (Narrow no-break space / Zsカテゴリ): 0xE2 0x80 0xAF
                            consume(3);
                            continue;
                        default:
                            return;
                        }
                    case 0x81: // U+205F (Medium mathematical space / Zsカテゴリ): 0xE2 0x81 0x9F
                        if (static_cast<unsigned char>(peekAhead(2)) == 0x9F) {
                            consume(3);
                            continue;
                        }
                        return;
                    default:
                        return;
                    }
                case 0xE3:  // U+3000 (Ideographic space / Zsカテゴリ): 0xE3 0x80 0x80
                    if (matchUtf8Bytes(1, 0x80, 0x80)) {
                        consume(3);
                        continue;
                    }
                    return;
                case 0xEF:  // U+FEFF (Byte order mark): 0xEF 0xBB 0xBF
                    if (matchUtf8Bytes(1, 0xBB, 0xBF)) {
                        consume(3);
                        continue;
                    }
                    return;

                case '/': // コメントの可能性
                    switch (peekAhead(1)) {
                    case '/':       // 単一行コメント //
                        consume(2);  // "//"
                        // 改行か終端まで読み飛ばし（JSON5仕様7 Comments準拠）
                        while (!isLineTerminator()) {
                            consume();
                        }
                        // 改行コードは次の反復で読み飛ばす。
                        continue;
                    case '*':       // 複数行コメント /* */
                        consume(2);  // "/*"
                        // */ まで読み飛ばし
                        for (;;) {
                            char ch = peek();
                            if (ch == '\0') {
                                break;  // 入力終端（閉じていないコメント）
                            }
                            if (ch == '*' && peekAhead(1) == '/') {
                                consume(2);  // "*/"
                                break;
                            }
                            consume();
                        }
                        continue;
                    default: // '/'だがコメントではない
                        return;
                    }

                default: // 空白でもコメントでもない、または終端。
                    return;
            }
        }
    }

    // ******************************************************************************** 文字列・識別子の解析
private:
    // @brief 文字列をパース
    // @param quote 開始引用符（'または"）
    std::string parseString(char quote) {
        std::string result;
        consume(); // 開始引用符を消費

        for (;;) {
            unsigned char c = static_cast<unsigned char>(peek());

            // 文字種別で分岐（終端、終了引用符、エスケープ、U+2028/U+2029、通常文字）
            switch (c) {
            case '\0':
                // 入力終端に到達（閉じ引用符なし）
                throw std::runtime_error("JSON5: unterminated string");
            case '"':
            case '\'':
                // 終了引用符かチェック
                if (static_cast<char>(c) == quote) {
                    consume(); // 終了引用符を消費
                    return result;
                }
                // 異なる引用符の場合は通常の文字として処理
                result += peek();
                consume();
                break;
            case '\\': {
                // エスケープシーケンス処理
                consume();  // '\'を消費
                unsigned char next = static_cast<unsigned char>(peek());
                consume();
                switch (next) {
                    case '"':  result += '"'; break;
                    case '\'': result += '\''; break;
                    case '\\': result += '\\'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'v':  result += '\v'; break;
                    case '0': {
                        // JSON5仕様5.1: \0の後に数字(1-9)が続いてはいけない
                        auto nn = peek();
                        if ('1' <= nn && nn <= '9') {
                            throw std::runtime_error(
                                "JSON5: decimal digit must not follow \\0 escape sequence");
                        }
                        result += '\0';
                        break;
                    }
                    case '\n': // 行継続（エスケープされた改行は無視）
                    case '\r':
                        // JSON5仕様 Table 2: Line terminator sequences
                        if (next == '\r' && peek() == '\n') {
                            consume();  // CRLF
                        }
                        break;
                    case 0xE2: {
                        // U+2028 (Line separator): 0xE2 0x80 0xA8
                        // U+2029 (Paragraph separator): 0xE2 0x80 0xA9
                        // 行継続として処理（JSON5仕様 Table 2準拠）
                        if (static_cast<unsigned char>(peek()) == 0x80) {
                            unsigned char third = static_cast<unsigned char>(peekAhead(1));
                            if (third == 0xA8 || third == 0xA9) {
                                consume(2);  // 0x80, 0xA8 or 0xA9
                                break;
                            }
                        }
                        // それ以外は通常のエスケープとして処理
                        result += static_cast<char>(0xE2);
                        result += static_cast<char>(next);
                        break;
                    }
                    case 'x': {
                        // Latin-1 Supplement エスケープ \xXX (2桁の16進数)
                        int codePoint = parseHexEscape(2, "\\x");
                        result += static_cast<char>(codePoint);
                        break;
                    }
                    case 'u': {
                        // Unicode エスケープ \uXXXX (4桁の16進数)
                        int codePoint = parseHexEscape(4, "\\u");
                        appendUtf8(result, codePoint);
                        break;
                    }
                    default:
                        // その他のエスケープは文字そのまま
                        result += static_cast<char>(next);
                        break;
                }
                break;
            }
            case 0xE2:
                // U+2028/U+2029がエスケープなしで出現した場合の警告（JSON5仕様5.2準拠）
                if (matchUtf8Bytes(0, 0xE2, 0x80)) {
                    unsigned char third = static_cast<unsigned char>(peekAhead(2));
                    switch (third) {
                    case 0xA8:  // U+2028 (Line separator)
                        warningOutput_.warning("Unescaped U+2028 (Line separator) in string at position " +
                                             std::to_string(inputSource_.position()));
                        result += peek();
                        result += peekAhead(1);
                        result += peekAhead(2);
                        consume(3);
                        break;
                    case 0xA9:  // U+2029 (Paragraph separator)
                        warningOutput_.warning("Unescaped U+2029 (Paragraph separator) in string at position " +
                                             std::to_string(inputSource_.position()));
                        result += peek();
                        result += peekAhead(1);
                        result += peekAhead(2);
                        consume(3);
                        break;
                    default:
                        // U+2028/U+2029以外のE2始まり文字
                        result += peek();
                        consume();
                        break;
                    }
                } else {
                    // 0xE2だが0x80が続かない場合
                    result += peek();
                    consume();
                }
                break;
            default:
                // 通常の文字
                result += peek();
                consume();
                break;
            }
        }
    }

    // @brief 識別子をパース（JSON5のキー名用）
    // @note ECMAScript 5.1のIdentifierNameに準拠（Unicode文字対応）
    std::string parseIdentifier() {
        std::string result;
        char c = peek();

        // 最初の文字は英字、$、_、またはUnicode文字
        switch (c) {
            case '$': case '_':
            CASE_ALPHA_LOWER:
            CASE_ALPHA_UPPER:
                result += peek();
                consume();
                break;
                // ※識別子では\xXX形式のエスケープ文字には対応しない。
            case '\\': // Unicodeエスケープ \uXXXX（識別子内）
                consume();  // '\'
                if (peek() == 'u') {
                    consume();  // 'u'
                    int codePoint = parseHexEscape(4, "\\u");
                    appendUtf8(result, codePoint);
                } else {
                    throw std::runtime_error("JSON5: invalid escape in identifier");
                }
                break;
            default:
                // Unicode文字をチェック（consumeUnicodeCharがfalseを返す場合はASCII文字で不正）
                if (!consumeUnicodeChar(result)) {
                    throw std::runtime_error("JSON5: expected identifier or object key");
                }
                break;
        }

        // 2文字目以降は英数字、$、_、またはUnicode文字
        for (;;) {
            c = peek();
            switch (c) {
                case '$': case '_':
                CASE_PART_DIGITS:
                CASE_ALPHA_LOWER:
                CASE_ALPHA_UPPER:
                    result += peek();
                    consume();
                    break;
                case '\\':
                    // Unicodeエスケープ \uXXXX（識別子内）
                    consume();  // '\'
                    if (peek() == 'u') {
                        consume();  // 'u'
                        int codePoint = parseHexEscape(4, "\\u");
                        appendUtf8(result, codePoint);
                    } else {
                        throw std::runtime_error("JSON5: invalid escape in identifier");
                    }
                    break;
                default:
                    // Unicode文字をチェック（consumeUnicodeCharがfalseを返す場合は識別子終端）
                    if (!consumeUnicodeChar(result)) {
                        // 識別子にできる文字以外、または終端(\0)
                        return result;
                    }
                    break;
            }
        }

        return result;
    }

    // @brief 16進数エスケープシーケンスをパース（\xXX または \uXXXX）
    // @param numDigits 16進数の桁数（2または4）
    // @param escapeName エラーメッセージ用のエスケープ名（"\\x" または "\\u"）
    // @return パースした値
    int parseHexEscape(int numDigits, const char* escapeName) {
        int codePoint = 0;
        for (int i = 0; i < numDigits; ++i) {
            char hexChar = peek();
            consume();
            int digitValue = hexDigitToValue(hexChar);
            if (digitValue == -1) {
                throw std::runtime_error(
                    std::string("JSON5: invalid escape sequence '") + escapeName +
                    "', expected hex digit but got '" + hexChar + "'");
            }
            codePoint = (codePoint << 4) | digitValue;
        }
        return codePoint;
    }

    // @brief Unicode コードポイントをUTF-8にエンコードして文字列に追加
    // @param result 追加先の文字列
    // @param codePoint Unicodeコードポイント
    static void appendUtf8(std::string& result, int codePoint) {
        if (codePoint < 0) {
            throw std::runtime_error("JSON5: invalid code point");
        } else if (codePoint <= 0x7F) {
            // 1バイト (ASCII)
            result += static_cast<char>(codePoint);
        } else if (codePoint <= 0x7FF) {
            // 2バイト
            result += static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F));
            result += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else if (codePoint <= 0xFFFF) {
            // 3バイト
            result += static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F));
            result += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else if (codePoint <= 0x10FFFF) {
            // 4バイト
            result += static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07));
            result += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else {
            throw std::runtime_error("JSON5: code point out of Unicode range");
        }
    }

    // @brief Unicode文字（マルチバイトUTF-8）を1文字消費して追加
    // @param result 追加先の文字列
    // @return Unicode文字として有効ならtrue、そうでなければfalse
    bool consumeUnicodeChar(std::string& result) {
        unsigned char first = static_cast<unsigned char>(peek());

        if (first < 0x80) {
            // ASCII（1バイト）- Unicode文字ではないのでfalse
            return false;
        } else if ((first & 0xE0) == 0xC0) {
            // 2バイト
            result += peek();
            result += peekAhead(1);
            consume(2);
            return true;
        } else if ((first & 0xF0) == 0xE0) {
            // 3バイト
            result += peek();
            result += peekAhead(1);
            result += peekAhead(2);
            consume(3);
            return true;
        } else if ((first & 0xF8) == 0xF0) {
            // 4バイト
            result += peek();
            result += peekAhead(1);
            result += peekAhead(2);
            result += peekAhead(3);
            consume(4);
            return true;
        } else {
            // 不正なUTF-8
            throw std::runtime_error("JSON5: invalid UTF-8 sequence");
        }
    }

    // ******************************************************************************** 数値の解析
private:
    // @brief 数値をパース
    // @note 符号付き数値、16進数、10進数（整数・浮動小数点）をサポート
    void parseNumber(char c, std::size_t tokenPos) {
        // 符号を保存
        bool isNegative = false;
        if (c == '+' || c == '-') {
            isNegative = (c == '-');
            consume();
            c = peek();
        }

        // 16進数の可能性をチェック
        char nextChar = peekAhead(1);
        if (c == '0' && (nextChar == 'x' || nextChar == 'X')) {
            consume(2); // '0', 'x' or 'X'
            std::int64_t value = 0;
            bool hasDigits = false;
            // 16進数の桁を読み取り（switch文で判定）
            while (true) {
                char digit = peek();
                int digitValue = hexDigitToValue(digit);
                if (digitValue == -1) {
                    break;  // 16進数の桁ではない
                }
                consume();
                hasDigits = true;
                value = value * 16 + digitValue;
            }
            if (!hasDigits) {
                throw std::runtime_error("JSON5: invalid hexadecimal number");
            }
            if (isNegative) {
                value = -value;
            }
            emitToken(json_token_detail::IntVal{value}, tokenPos);
            validateAfterValue();
            return;
        }

        // 10進数として処理
        parseDecimalNumber(c, isNegative, tokenPos);
        validateAfterValue();
    }

    // @brief 16進数の桁を数値に変換（switch版で最適化保証）
    static constexpr int hexDigitToValue(char c) {
        switch (c) {
            case '0': return 0;
            case '1': return 1;
            case '2': return 2;
            case '3': return 3;
            case '4': return 4;
            case '5': return 5;
            case '6': return 6;
            case '7': return 7;
            case '8': return 8;
            case '9': return 9;
            case 'a': case 'A': return 10;
            case 'b': case 'B': return 11;
            case 'c': case 'C': return 12;
            case 'd': case 'D': return 13;
            case 'e': case 'E': return 14;
            case 'f': case 'F': return 15;
            default: return -1;
        }
    }

    // @brief 10進数の桁かどうかを判定
    static constexpr bool isDecimalDigit(char c) {
        switch (c) {
            CASE_PART_DIGITS:
                return true;
            default:
                return false;
        }
    }

    // @brief 10進数をパース（整数部・小数部・指数部）
    // @param c 先頭文字
    // @param isNegative 符号が負かどうか
    void parseDecimalNumber(char c, bool isNegative, std::size_t tokenPos) {
        std::int64_t integerPart = 0;
        double fractionalPart = 0.0;
        bool hasDot = false;
        bool hasIntegerDigits = false;
        bool hasFractionalDigits = false;

        // 先頭が'.'の場合
        if (c == '.') {
            hasDot = true;
            consume();
        } else {
            // 整数部を読み取り
            while (isDecimalDigit(peek())) {
                hasIntegerDigits = true;
                char digit = peek();
                consume();
                integerPart = integerPart * 10 + (digit - '0');
            }

            // 小数点
            if (peek() == '.') {
                hasDot = true;
                consume();
            }
        }

        // 小数点の後の数字を読み取り
        if (hasDot) {
            double divisor = 10.0;
            while (isDecimalDigit(peek())) {
                hasFractionalDigits = true;
                char digit = peek();
                consume();
                fractionalPart += (digit - '0') / divisor;
                divisor *= 10.0;
            }
        }

        // 最低1桁の数字が必要
        if (!hasIntegerDigits && !hasFractionalDigits) {
            throw std::runtime_error("JSON5: invalid number format");
        }

        // 指数部を読み取り
        int exponent = 0;
        bool hasExp = false;
        if (peek() == 'e' || peek() == 'E') {
            hasExp = true;
            consume();
            bool expNegative = false;
            if (peek() == '+' || peek() == '-') {
                expNegative = (peek() == '-');
                consume();
            }
            bool hasExpDigits = false;
            while (isDecimalDigit(peek())) {
                hasExpDigits = true;
                char digit = peek();
                consume();
                exponent = exponent * 10 + (digit - '0');
            }
            if (!hasExpDigits) {
                throw std::runtime_error("JSON5: invalid exponent");
            }
            if (expNegative) {
                exponent = -exponent;
            }
        }

        // 値を計算
        if (hasDot || hasExp) {
            // 浮動小数点数
            double value = static_cast<double>(integerPart) + fractionalPart;
            if (hasExp) {
                value *= std::pow(10.0, exponent);
            }
            if (isNegative) {
                value = -value;
            }
            emitToken(json_token_detail::NumVal{value}, tokenPos);
        } else {
            // 整数
            std::int64_t value = integerPart;
            if (isNegative) {
                value = -value;
            }
            emitToken(json_token_detail::IntVal{value}, tokenPos);
        }
    }

    // ********************************************************************************
    // 入力文字取得・判定
   private:
    // @brief 現在位置の文字を取得（範囲外の場合は'\0'）
    char peek() const { return inputSource_.peekAhead(0); }

    // @brief 指定した位置先の文字を取得（範囲外の場合は'\0'）
    // @param offset 現在位置からのオフセット（0の場合はpeek()と同じ）
    char peekAhead(std::size_t offset) const {
        return inputSource_.peekAhead(offset);
    }

    // @brief 現在位置から指定された文字数だけ読み進める
    // @param count 進める文字数（デフォルトは1）
    // @note 文字を取得する場合は、事前にpeek()またはpeekAhead()を呼び出すこと
    void consume(std::size_t count = 1) { inputSource_.consume(count); }

    // @brief 指定位置以降のバイト列が指定パターンと一致するかチェック（2バイト版）
    // @param offset 現在位置からのオフセット
    // @param byte1 1バイト目の期待値
    // @param byte2 2バイト目の期待値
    // @return 一致すればtrue
    bool matchUtf8Bytes(std::size_t offset, unsigned char byte1, unsigned char byte2) const {
        return static_cast<unsigned char>(peekAhead(offset)) == byte1 &&
               static_cast<unsigned char>(peekAhead(offset + 1)) == byte2;
    }

    // @brief 現在位置が改行(LineTerminator)かどうかを判定（JSON5仕様準拠）
    // @return 改行か\0ならtrue（U+000A, U+000D, U+2028, U+2029）
    bool isLineTerminator() const {
        switch (static_cast<unsigned char>(peek())) {
        case '\n':  // U+000A (Line feed)
        case '\r':  // U+000D (Carriage return)
        case '\0':  // 終端。
            return true;
        case 0xE2:
            // U+2028 (Line separator): 0xE2 0x80 0xA8
            // U+2029 (Paragraph separator): 0xE2 0x80 0xA9
            return matchUtf8Bytes(0, 0xE2, 0x80) &&
                   (static_cast<unsigned char>(peekAhead(2)) == 0xA8 ||
                    static_cast<unsigned char>(peekAhead(2)) == 0xA9);
        default:
            return false;
        }
    }

    // ******************************************************************************** トークン生成
private:
    // @brief すべてのトークンを生成
    // @note 入力全体を走査してトークン列を生成し、終端トークンを追加する
    void generateAllTokens() {
        while (generateNextToken())
            ;
        // ストリーム終端マーカーを追加
        emitToken(json_token_detail::EndOfStreamTag{}, inputSource_.position());
    }

    // @brief 次のトークンを1つ生成してtokenManager_に追加
    // @note 識別子・文字列の後にコロンがあればキー、なければ値として判定
    // @return まだ続きがあるならtrue、終端ならfalse。
    bool generateNextToken() {
        skipWhitespaceAndComments();
        const std::size_t tokenPos = inputSource_.position();
        char c = peek();

        // 先頭文字でトークンの種類を判定
        switch (c) {
            case '\0':
                // 入力終端
                return false;
            case '{':
                consume();
                emitToken(json_token_detail::StartObjectTag{}, tokenPos);
                break;
            case '}':
                consume();
                emitToken(json_token_detail::EndObjectTag{}, tokenPos);
                break;
            case '[':
                consume();
                emitToken(json_token_detail::StartArrayTag{}, tokenPos);
                break;
            case ']':
                consume();
                emitToken(json_token_detail::EndArrayTag{}, tokenPos);
                break;
            case ':': // コロンはスキップ（トークンとして生成しない）
                consume();
                break;
            case ',': // カンマはスキップ（トークンとして生成しない）
                consume();
                break;
            case '"':
            case '\'': // 文字列: 後にコロンがあればキー、なければ値
                addStringOrKey(parseString(c), tokenPos);
                break;
            case '+': case '-': case '.':
            CASE_PART_DIGITS:
                parseNumber(c, tokenPos);
                break;
            case 'n': // 予約語: null または識別子
                addReservedWordOrIdentifier("null", json_token_detail::NullTag{}, tokenPos);
                break;
            case 't': // 予約語: true または識別子
                addReservedWordOrIdentifier("true", json_token_detail::BoolVal{true}, tokenPos);
                break;
            case 'f': // 予約語: false または識別子
                addReservedWordOrIdentifier("false", json_token_detail::BoolVal{false}, tokenPos);
                break;
            case 'I': // 予約語: Infinity または識別子
                addReservedWordOrIdentifier("Infinity",
                    json_token_detail::NumVal{std::numeric_limits<double>::infinity()}, tokenPos);
                break;
            case 'N': // 予約語: NaN または識別子
                addReservedWordOrIdentifier("NaN",
                    json_token_detail::NumVal{std::numeric_limits<double>::quiet_NaN()}, tokenPos);
                break;
            default:
                // その他の文字：識別子として処理（英字、$、_で始まる）
                // parseIdentifierが英字・$・_以外の場合は例外を投げる
                addStringOrKey(parseIdentifier(), tokenPos);
                break;
        }
        return true;
    }

    // @brief 予約語または識別子をトークンとして追加する。
    // @tparam N 予約語の長さ
    // @param reservedWord 予約語文字列
    // @param valueToken 予約語が値の場合のトークン
    template <std::size_t N, typename TokenT>
    void addReservedWordOrIdentifier(const char (&reservedWord)[N], TokenT valueToken,
                                     std::size_t tokenPos) {
        if (matchReservedWord(reservedWord)) {
            addReservedWordOrKey(reservedWord, std::move(valueToken), tokenPos);
        } else {
            addStringOrKey(parseIdentifier(), tokenPos);
        }
    }

    // @brief 予約語を文字単位で比較（コンパイル時展開）
    // @tparam N 予約語の長さ
    // @param reservedWord 予約語の文字配列
    // @return 一致すればtrue
    template <std::size_t N>
    bool matchReservedWord(const char (&reservedWord)[N]) {
        constexpr std::size_t len = N - 1;  // null終端を除く
        // インデックスシーケンスを使って完全に展開
        return matchReservedWordImpl(reservedWord, std::make_index_sequence<len>{});
    }

    // @brief 予約語比較の実装（インデックスシーケンスで完全展開）
    template <std::size_t N, std::size_t... I>
    bool matchReservedWordImpl(const char (&reservedWord)[N], std::index_sequence<I...>) {
        // すべての文字を&&で結合して一度に比較（完全展開）
        if (((peekAhead(I) == reservedWord[I]) && ...)) {
            // 一致した文字数だけconsumeする（Iを使ってfold展開）
            ((consume(), (void)I), ...);
            return true;
        }
        return false;
    }

    // @brief 予約語をキーか値として追加
    // @param word 予約語の文字列
    // @param valueToken 値としてのトークン
    template <typename TokenT>
    void addReservedWordOrKey(const char* word, TokenT valueToken, std::size_t tokenPos) {
        skipWhitespaceAndComments();
        if (peek() == ':') {
            emitToken(json_token_detail::KeyVal{word}, tokenPos);
        } else {
            emitToken(std::move(valueToken), tokenPos);
            validateAfterValue();
        }
    }

    // @brief 識別子または文字列をキーか値として追加
    // @note 処理速度低下を避けるためaddReservedWordOrKeyとの共通化しない。
    // @param text 識別子または文字列
    void addStringOrKey(std::string text, std::size_t tokenPos) {
        skipWhitespaceAndComments();
        if (peek() == ':') {
            emitToken(json_token_detail::KeyVal{std::move(text)}, tokenPos);
        } else {
            emitToken(json_token_detail::StrVal{std::move(text)}, tokenPos);
            validateAfterValue();
        }
    }

    // @brief 値トークン追加後に次の文字が有効かチェック
    // @note 値の後は , } ] または末尾のみ許可
    void validateAfterValue() {
        skipWhitespaceAndComments();
        char c = peek();
        if (c != '\0' && c != ',' && c != '}' && c != ']') {
            throw std::runtime_error(
                std::string("JSON5: unexpected character '") + c + "' after value");
        }
    }

    template <typename TokenT>
    void emitToken(TokenT&& tokenValue, std::size_t position) {
        tokenManager_.pushToken(JsonToken{std::forward<TokenT>(tokenValue), position});
    }

    // ******************************************************************************** 構築
public:
    // @brief コンストラクタ（入力文字列取得元を指定）
    // @param inputSource 入力文字列取得元の参照
    // @param tokenManager トークン管理オブジェクトの参照
    explicit JsonTokenizer(Input& inputSource, TokMgr& tokenManager)
        : inputSource_(inputSource), tokenManager_(tokenManager) {}

    // @brief コンストラクタ（入力文字列取得元、トークン管理、警告出力オブジェクトを指定）
    // @param inputSource 入力文字列取得元の参照
    // @param tokenManager トークン管理オブジェクトの参照
    // @param warnOut 警告メッセージの出力先
    JsonTokenizer(Input& inputSource, TokMgr& tokenManager, MessageOutput& warnOut)
        : inputSource_(inputSource), tokenManager_(tokenManager), warningOutput_(warnOut) {}

    // @brief トークン生成を実行
    // @note 入力全体をパースしてトークン列を生成する
    void tokenize() {
        try {
            generateAllTokens();
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("JSON5 parse error at position ") +
                                     std::to_string(inputSource_.position()) + ": " + e.what());
        }
    }

    // ******************************************************************************** メンバー変数
private:
    Input& inputSource_;         ///< 入力文字列取得元の参照
    TokMgr& tokenManager_;       ///< トークン管理オブジェクトの参照
    MessageOutput& warningOutput_;      ///< 警告メッセージ出力先
};

}  // namespace rai::serialization
