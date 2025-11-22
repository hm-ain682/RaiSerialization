// @file SortedFieldMap.cppm
// @brief ハッシュベースのソート済みフィールドマップの定義。

module;
#include <array>
#include <string_view>
#include <optional>
#include <algorithm>
#include <cstring>
#include <cstdint>

export module rai.compiler.io.sorted_field_map;

export namespace rai::json {

/// @brief ハッシュベースのソート済みフィールドマップ。
/// @tparam KeyType キーの型。
/// @tparam ValueType 値の型。
/// @tparam N フィールド数。
/// @note キー名のハッシュ値でソートされたフィールド情報を保持し、高速な検索を提供する。
template <typename KeyType, typename ValueType, std::size_t N>
class SortedFieldMap {
public:
    /// @brief フィールド情報の構造体。
    struct FieldInfo {
        KeyType key;                ///< フィールドのキー。
        ValueType value;            ///< フィールドの値。
        std::uint32_t hash;         ///< キーから計算したFNV1aハッシュ値。
        std::size_t originalIndex;  ///< 元のフィールド定義順序。
    };

    using FieldInfoArrayType = std::array<FieldInfo, N>;

    /// @brief デフォルトコンストラクタ。
    constexpr SortedFieldMap() = default;

    /// @brief フィールド情報を初期化する。
    /// @tparam Fields フィールドの型パラメータパック。
    /// @param fields フィールド定義群。
    /// @note フィールド情報を配列に詰めて、ハッシュ値でソートする。
    template <typename... Fields>
    constexpr void initialize(const Fields&... fields) {
        // フィールド情報を配列に詰める
        std::size_t i = 0;
        ((sortedFields_[i] = {
            static_cast<KeyType>(fields.key),
            static_cast<ValueType>(fields.required),
            fnv1a32(fields.key),
            i
        }, ++i), ...);

        // sortedFields_ を (hash, key lex) でソート
        std::sort(sortedFields_.begin(), sortedFields_.end(),
            [](const FieldInfo& a, const FieldInfo& b) {
                if (a.hash != b.hash) {
                    return a.hash < b.hash;
                }
                return std::strcmp(a.key, b.key) < 0;
            });
    }

    /// @brief 指定キーに対応するフィールドを探索する。
    /// @param key 探索するキー名。
    /// @return 見つかった場合はフィールドの元インデックス、未検出時はstd::nullopt。
    /// @note ハッシュ値で二分探索を行い、同じハッシュ値の範囲内で線形探索する。
    std::optional<std::size_t> findIndex(std::string_view key) const {
        const auto hash = fnv1a32(key.data(), key.size());
        auto lower = std::lower_bound(
            sortedFields_.begin(), sortedFields_.end(), hash,
            [](const FieldInfo& info, std::uint32_t valueHash) {
                return info.hash < valueHash;
            });
        for (auto it = lower; it != sortedFields_.end() && it->hash == hash; ++it) {
            if (std::string_view{it->key} == key) {
                return it->originalIndex;
            }
        }
        return std::nullopt;
    }

    /// @brief 指定インデックスのフィールド情報を取得する。
    /// @param originalIndex 元のフィールド定義順序のインデックス。
    /// @return フィールド情報。該当する情報がない場合はstd::nullopt。
    /// @note ソート後の配列から元のインデックスに対応する情報を探索する。
    std::optional<FieldInfo> getFieldInfo(std::size_t originalIndex) const {
        for (const auto& info : sortedFields_) {
            if (info.originalIndex == originalIndex) {
                return info;
            }
        }
        return std::nullopt;
    }

    /// @brief ソート済みフィールド配列への参照を取得する。
    /// @return ソート済みフィールド配列への参照。
    const FieldInfoArrayType& getSortedFields() const {
        return sortedFields_;
    }

private:
    FieldInfoArrayType sortedFields_{}; ///< ハッシュ順に整列したフィールド情報。

    /// @brief FNV-1a 32bitハッシュ関数（長さ指定版）。
    /// @param s ハッシュ対象の文字列。
    /// @param len 文字列の長さ。
    /// @return 計算されたハッシュ値。
    /// @note キー名の高速な比較のために使用する。
    static constexpr std::uint32_t fnv1a32(const char* s, std::size_t len) {
        std::uint32_t h = 2166136261u;
        for (std::size_t i = 0; i < len; ++i) {
            h ^= static_cast<std::uint8_t>(s[i]);
            h *= 16777619u;
        }
        return h;
    }

    /// @brief FNV-1a 32bitハッシュ関数（null終端文字列版）。
    /// @param s ハッシュ対象のnull終端文字列。
    /// @return 計算されたハッシュ値。
    /// @note キー名の高速な比較のために使用する。
    static constexpr std::uint32_t fnv1a32(const char* s) {
        std::uint32_t h = 2166136261u;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
            h ^= *p;
            h *= 16777619u;
        }
        return h;
    }
};

}  // namespace rai::json
