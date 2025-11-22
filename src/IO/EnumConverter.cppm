// @file EnumConverter.cppm
// @brief Enum型と文字列の相互変換を提供する。

module;
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>
#include <utility>

export module rai.compiler.io.enum_converter;

import rai.compiler.io.sorted_field_map;

export namespace rai::json {

/// @brief Enumと文字列のマッピングエントリ。
/// @tparam EnumType 対象のenum型。
template <typename EnumType>
struct EnumEntry {
    EnumType value;      ///< Enum値。
    const char* name;    ///< 対応する文字列名。
};

/// @brief Enum型と文字列の相互変換を行うコンバータ。
/// @tparam EnumType 対象のenum型。
/// @tparam N 変換エントリ数。
/// @note SortedFieldMapを使用して高速な文字列→enum検索を実現する。
template <typename EnumType, std::size_t N>
class EnumConverter {
public:
    /// @brief エントリ型の別名（後方互換性のため）。
    using Entry = EnumEntry<EnumType>;

    /// @brief コンストラクタ。変換テーブルを初期化する。
    /// @param entries Enumと文字列のマッピング配列。
    /// @note エントリは任意の順序で指定可能。内部でソートされる。
    constexpr explicit EnumConverter(const Entry (&entries)[N]) {
        // エントリをコピー
        for (std::size_t i = 0; i < N; ++i) {
            entries_[i] = entries[i];
        }

        // SortedFieldMapの初期化用に一時的なヘルパー構造体を使用
        initializeMap();
    }

    /// @brief Enum値を文字列に変換する。
    /// @param value 変換対象のEnum値。
    /// @return 対応する文字列。該当なしの場合はnullopt。
    /// @note 線形探索を行うため、頻繁な変換には注意が必要。
    std::optional<const char*> toString(EnumType value) const {
        for (const auto& entry : entries_) {
            if (entry.value == value) {
                return entry.name;
            }
        }
        return std::nullopt;
    }

    /// @brief 文字列をEnum値に変換する。
    /// @param name 変換対象の文字列。
    /// @return 対応するEnum値。該当なしの場合はnullopt。
    /// @note SortedFieldMapによる高速検索を使用する。
    std::optional<EnumType> fromString(std::string_view name) const {
        auto index = fieldMap_.findIndex(name);
        if (index) {
            return entries_[*index].value;
        }
        return std::nullopt;
    }

    /// @brief Enum値を文字列に変換する（例外版）。
    /// @param value 変換対象のEnum値。
    /// @return 対応する文字列。
    /// @throw std::invalid_argument 該当する文字列が見つからない場合。
    const char* toStringOrThrow(EnumType value) const {
        auto result = toString(value);
        if (result) {
            return *result;
        }
        throw std::invalid_argument("EnumConverter: invalid enum value");
    }

    /// @brief 文字列をEnum値に変換する（例外版）。
    /// @param name 変換対象の文字列。
    /// @return 対応するEnum値。
    /// @throw std::invalid_argument 該当するEnum値が見つからない場合。
    EnumType fromStringOrThrow(std::string_view name) const {
        auto result = fromString(name);
        if (result) {
            return *result;
        }
        throw std::invalid_argument(
            std::string("EnumConverter: invalid enum name '") + std::string(name) + "'");
    }

private:
    Entry entries_[N]{};  ///< Enumと文字列のマッピング配列。
    SortedFieldMap<const char*, EnumType, N> fieldMap_{};  ///< 文字列→Enum検索用マップ。

    /// @brief SortedFieldMapを初期化する。
    /// @note エントリ配列から文字列キーとEnum値を抽出してマップを構築する。
    constexpr void initializeMap() {
        // ヘルパー構造体を使ってSortedFieldMapを初期化
        struct FieldHelper {
            const char* key;
            EnumType required;  // この名前はSortedFieldMapの実装に合わせている
        };

        FieldHelper helpers[N];
        for (std::size_t i = 0; i < N; ++i) {
            helpers[i] = {entries_[i].name, entries_[i].value};
        }

        // 可変長引数で初期化するためのヘルパー
        initializeMapImpl(helpers, std::make_index_sequence<N>{});
    }

    /// @brief SortedFieldMapを初期化する実装。
    /// @tparam Indices インデックスシーケンス。
    /// @param helpers ヘルパー構造体配列。
    /// @note 可変長テンプレート引数でSortedFieldMapのinitializeを呼び出す。
    template <std::size_t... Indices>
    constexpr void initializeMapImpl(const auto* helpers, std::index_sequence<Indices...>) {
        fieldMap_.initialize(helpers[Indices]...);
    }
};

/// @brief EnumConverterを作成するヘルパー関数。
/// @tparam EnumType 対象のenum型。
/// @tparam N 変換エントリ数（自動推論）。
/// @param entries Enumと文字列のマッピング配列。
/// @return 作成されたEnumConverter。
/// @note テンプレート引数の推論を簡略化するために使用する。
template <typename EnumType, std::size_t N>
constexpr auto makeEnumConverter(const EnumEntry<EnumType> (&entries)[N]) {
    return EnumConverter<EnumType, N>(entries);
}

}  // namespace rai::json
