// @file SortedHashArrayMap.cppm
// @brief ハッシュベースのソート済みフィールドマップの定義。

module;
#include <array>
#include <string_view>
#include <concepts>
#include <optional>
#include <algorithm>
#include <cstring>
#include <functional>
#include <type_traits>
#include <cstdint>

export module rai.collection.sorted_hash_array_map;

export namespace rai::collection {

// Traits you can specialize if you want custom hash/equality/compare behavior
template <typename K>
struct SortedHashArrayMapTraits {
    using Hash = std::hash<K>;
    using KeyEqual = std::equal_to<>;
    using KeyCompare = std::less<>;
};

// Lightweight key/value pair utility used by callers constructing
// SortedHashArrayMap from arrays of descriptors.
template <typename KeyT, typename ValueT>
struct KeyValue {
    KeyT key;
    ValueT value;
};

// Field descriptor concept — declared at namespace scope so it can be
// parameterized by Key/Value types and reused inside the class template.
template <typename KeyT, typename ValueT, typename FieldT>
concept FieldDescriptorFor = requires(const FieldT& f) {
    { f.key } -> std::convertible_to<KeyT>;
    { f.value } -> std::convertible_to<ValueT>;
};

/// @brief ハッシュベースのソート済みフィールドマップ。
/// @tparam KeyType キーの型。
/// @tparam ValueType 値の型。
/// @tparam N フィールド数。
/// @note キー名のハッシュ値でソートされたフィールド情報を保持し、高速な検索を提供する。
template <
    typename KeyType,
    typename ValueType,
    std::size_t N,
    typename Traits = SortedHashArrayMapTraits<KeyType>
>
class SortedHashArrayMap {
    using Hash = typename Traits::Hash;
    using KeyEqual = typename Traits::KeyEqual;
    using KeyCompare = typename Traits::KeyCompare;

    // Basic static checks: require Hash/KeyEqual to be invocable with KeyType
    static_assert(std::is_invocable_v<Hash, const KeyType&>, "Hash must be invocable with KeyType");
    static_assert(std::is_invocable_v<KeyEqual, const KeyType&, const KeyType&>, "KeyEqual must be invocable with KeyType, KeyType");

public:
    /// @brief フィールド情報の構造体。
    struct FieldInfo {
        KeyType key;                ///< フィールドのキー。
        ValueType value;            ///< フィールドの値。
        std::size_t hash;         ///< キーから計算したハッシュ値（Hash の結果）。
        std::size_t originalIndex;  ///< 元のフィールド定義順序。
    };

    using FieldInfoArrayType = std::array<FieldInfo, N>;

    /// @brief デフォルトコンストラクタ。
    constexpr SortedHashArrayMap() = default;

    /// @brief フィールド情報を受け取るテンプレートコンストラクタ。
    /// @tparam Fields フィールド型のパラメータパック。
    /// @param fields フィールド定義群。要素：keyとvalueメンバーを持つ型。例：KeyValue<KeyType, ValueType>
    /// @note フィールド情報を配列に詰めて、ハッシュ値でソートする。
    template <typename... Fields>
        requires ((FieldDescriptorFor<KeyType, ValueType, Fields>) && ...)
    constexpr explicit SortedHashArrayMap(const Fields&... fields) {
        static_assert(sizeof...(Fields) == N, "SortedHashArrayMap: number of fields must equal N");
        auto toValue = []<typename FT>(const FT& f) constexpr -> ValueType {
            return static_cast<ValueType>(f.value);
        };
        std::size_t i = 0;
        ((sortedFields_[i] = FieldInfo{ static_cast<KeyType>(fields.key),
                                       toValue(fields),
                                       Hash{}(fields.key),
                                       i }, ++i), ...);
        sortFields();
    }

    // Construct from a std::array of field-descriptor-like objects (size must match N)
    template <typename FieldT>
        requires FieldDescriptorFor<KeyType, ValueType, FieldT>
    constexpr explicit SortedHashArrayMap(const std::array<FieldT, N>& arr) {
        fillFromRange(arr.begin(), arr.end());
        sortFields();
    }

    // Construct from a C-style array: foo arr[M]
    template <typename FieldT>
        requires FieldDescriptorFor<KeyType, ValueType, FieldT>
    constexpr explicit SortedHashArrayMap(const FieldT (&arr)[N]) {
        fillFromRange(std::begin(arr), std::end(arr));
        sortFields();
    }

private:
    /// @brief フィールド情報を初期化する。
    /// @tparam Fields フィールドの型パラメータパック。
    /// @param fields フィールド定義群。
    /// @note フィールド情報を配列に詰めて、ハッシュ値でソートする。
    template <typename Iter>
        requires FieldDescriptorFor<KeyType, ValueType, std::remove_cvref_t<decltype(*std::declval<Iter>())>>
    constexpr void fillFromRange(Iter begin, Iter end) {
        std::size_t i = 0;
        for (; begin != end; ++begin, ++i) {
            const auto& f = *begin;
            sortedFields_[i] = {
                static_cast<KeyType>(f.key),
                static_cast<ValueType>(f.value),
                Hash{}(f.key),
                i
            };
        }
    }

    /// @brief sortedFields_をソートする。
    constexpr void sortFields() {
        std::sort(sortedFields_.begin(), sortedFields_.end(),
            [](const FieldInfo& a, const FieldInfo& b) {
                if (a.hash != b.hash) {
                    return a.hash < b.hash;
                }
                return KeyCompare{}(a.key, b.key);
            });
    }

public:
    /// @brief 指定キーに対応するフィールドを探索する。
    /// @param key 探索するキー名。
    /// @return 見つかった場合はフィールドの元インデックス、未検出時はstd::nullopt。
    /// @note ハッシュ値で二分探索を行い、同じハッシュ値の範囲内で線形探索する。
    template <typename Lookup>
    std::optional<std::size_t> findIndex(const Lookup& key) const {
        const auto hash = Hash{}(key);
        auto lower = std::lower_bound(
            sortedFields_.begin(), sortedFields_.end(), hash,
            [](const FieldInfo& info, std::size_t valueHash) {
                return info.hash < valueHash;
            });
        for (auto it = lower; it != sortedFields_.end() && it->hash == hash; ++it) {
            if (KeyEqual{}(it->key, key)) {
                return it->originalIndex;
            }
        }
        return std::nullopt;
    }

    /// @brief 指定キーに対応する値を取得する。見つからなければ nullptr を返す。
    /// @tparam Lookup 検索キー型。
    template <typename Lookup>
    const ValueType* findValue(const Lookup& key) const {
        const auto hash = Hash{}(key);
        auto lower = std::lower_bound(
            sortedFields_.begin(), sortedFields_.end(), hash,
            [](const FieldInfo& info, std::size_t valueHash) {
                return info.hash < valueHash;
            });
        for (auto it = lower; it != sortedFields_.end() && it->hash == hash; ++it) {
            if (KeyEqual{}(it->key, key)) {
                return &it->value;
            }
        }
        return nullptr;
    }

    /// @brief すべてのエントリに対してイテレーションを行うユーティリティ。
    /// @tparam Visitor 呼び出し可能オブジェクト (const ValueType&)->void
    template <typename Visitor>
    void forEach(Visitor&& visitor) const {
        for (const auto& info : sortedFields_) {
            std::forward<Visitor>(visitor)(info.value);
        }
    }

private:
    std::array<FieldInfo, N> sortedFields_{}; ///< ハッシュ順に整列したフィールド情報。
};

}  // namespace rai::json
