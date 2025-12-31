// @file SortedHashArrayMap.cppm
// @brief ハッシュベースのソート済みフィールドマップの定義。

module;
#include <array>
#include <string_view>
#include <concepts>
#include <optional>
#include <algorithm>
#include <cstring>
#include <span>
#include <functional>
#include <type_traits>
#include <cstdint>

export module rai.collection.sorted_hash_array_map;

export namespace rai::collection {

// Field descriptor concept — declared at namespace scope so it can be
// parameterized by Key/Value types and reused inside the class template.
template <typename KeyT, typename ValueT, typename FieldT>
concept IsKeyValuePair = requires(const FieldT& f) {
    { f.first } -> std::convertible_to<KeyT>;
    { f.second } -> std::convertible_to<ValueT>;
};

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

/// @brief SortedHashArrayMapで保持するエントリ情報。
/// @tparam KeyType キーの型。
/// @tparam ValueType 値の型。
template <typename KeyType, typename ValueType>
struct MapEntry {
    KeyType key;                ///< フィールドのキー。
    ValueType value;            ///< フィールドの値。
    std::size_t hash;           ///< キーから計算したハッシュ値（Hash の結果）。
    std::size_t originalIndex;  ///< 元のフィールド定義順序。
};

template <
    typename KeyType,
    typename ValueType,
    std::size_t N,
    typename Traits
>
class SortedHashArrayMap;

/// @brief SortedHashArrayMap/MapReference が共有する探索アルゴリズム。
/// @tparam KeyType キーの型。
/// @tparam ValueType 値の型。
/// @tparam Traits ハッシュ／比較の振る舞い。
template <
    typename KeyType,
    typename ValueType,
    typename Traits
>
struct SortedHashArrayMapAlgorithms {
    using Hash = typename Traits::Hash;
    using KeyEqual = typename Traits::KeyEqual;
    using Entry = MapEntry<KeyType, ValueType>;

    /// @brief 指定キーに対応するエントリの元インデックスを検索する。
    /// @param entriesBegin エントリ配列の先頭。
    /// @param entriesSize エントリ数。
    /// @param key 検索キー。
    /// @return 見つかった場合は元インデックス、未検出時はstd::nullopt。
    template <typename Lookup>
    static std::optional<std::size_t> findIndex(
        std::span<const Entry> entries, const Lookup& key) {
        const auto hash = Hash{}(key);

        const auto lower = std::lower_bound(
            entries.begin(),
            entries.end(),
            hash,
            [](const Entry& entry, std::size_t hashValue) {
                return entry.hash < hashValue;
            });

        for (auto it = lower; it != entries.end() && it->hash == hash; ++it) {
            if (KeyEqual{}(it->key, key)) {
                return it->originalIndex;
            }
        }
        return std::nullopt;
    }

    /// @brief 指定キーに対応する値を検索する。
    /// @param entriesBegin エントリ配列の先頭。
    /// @param entriesSize エントリ数。
    /// @param key 検索キー。
    /// @return 見つかった場合は値へのポインタ、未検出時はnullptr。
    template <typename Lookup>
    static const ValueType* findValue(
        std::span<const Entry> entries,
        const Lookup& key) {
        const auto hash = Hash{}(key);

        const auto lower = std::lower_bound(
            entries.begin(),
            entries.end(),
            hash,
            [](const Entry& entry, std::size_t hashValue) {
                return entry.hash < hashValue;
            });

        for (auto it = lower; it != entries.end() && it->hash == hash; ++it) {
            if (KeyEqual{}(it->key, key)) {
                return &it->value;
            }
        }
        return nullptr;
    }
};

/// @brief SortedHashArrayMapへの参照を保持する薄いラッパー（Nを型に含めない）。
/// @tparam KeyType キーの型。
/// @tparam ValueType 値の型。
/// @tparam Traits ハッシュ／比較の振る舞い。
/// @note 参照先は SortedHashArrayMap を想定する。
template <
    typename KeyType,
    typename ValueType,
    typename Traits = SortedHashArrayMapTraits<KeyType>
>
class MapReference {
public:
    using Entry = MapEntry<KeyType, ValueType>;
    using iterator = const Entry*;
    using Algorithms = SortedHashArrayMapAlgorithms<KeyType, ValueType, Traits>;

    /// @brief デフォルトコンストラクタ。
    /// @details 空の参照として構築する。
    MapReference() = default;

    template <std::size_t N>
    constexpr explicit MapReference(const SortedHashArrayMap<KeyType, ValueType, N, Traits>& map) noexcept
        : entries_(map.begin(), N) {}

    /// @brief 指定キーに対応するエントリの元インデックスを検索する。
    /// @tparam Lookup 検索キーの型。
    /// @param key 検索キー。
    /// @return 見つかった場合は元インデックス、未検出時はstd::nullopt。
    template <typename Lookup>
    std::optional<std::size_t> findIndex(const Lookup& key) const {
        return Algorithms::findIndex(entries_, key);
    }

    /// @brief 指定キーに対応する値を検索する。
    /// @tparam Lookup 検索キーの型。
    /// @param key 検索キー。
    /// @return 見つかった場合は値へのポインタ、未検出時はnullptr。
    template <typename Lookup>
    const ValueType* findValue(const Lookup& key) const {
        return Algorithms::findValue(entries_, key);
    }

    /// @brief 反復の開始位置を取得する。
    /// @return 先頭イテレータ。未設定時はnullptr。
    iterator begin() const {
        return entries_.data();
    }

    /// @brief 反復の終了位置を取得する。
    /// @return 終端イテレータ。未設定時はnullptr。
    iterator end() const {
        return entries_.data() + entries_.size();
    }

private:
    std::span<const Entry> entries_{}; ///< エントリ配列参照。
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

public:
    using FieldInfo = MapEntry<KeyType, ValueType>;
    using FieldInfoArrayType = std::array<FieldInfo, N>;
    using Algorithms = SortedHashArrayMapAlgorithms<KeyType, ValueType, Traits>;

    /// @brief デフォルトコンストラクタ。
    constexpr SortedHashArrayMap() = default;

    /// @brief フィールド情報を受け取るテンプレートコンストラクタ。
    /// @tparam Fields フィールド型のパラメータパック。
    /// @param fields フィールド定義群。要素：pair<KeyType, ValueType>型。
    /// @note フィールド情報を配列に詰めて、ハッシュ値でソートする。
    template <typename... Fields>
        requires (IsKeyValuePair<KeyType, ValueType, Fields> && ...)
    constexpr explicit SortedHashArrayMap(const Fields&... fields) {
        static_assert(sizeof...(Fields) == N, "SortedHashArrayMap: number of fields must equal N");
        std::size_t i = 0;
        ((sortedFields_[i] = FieldInfo{
            static_cast<KeyType>(fields.first),
            static_cast<ValueType>(fields.second),
            Hash{}(fields.first),
            i
        }, ++i), ...);
        sortFields();
    }

    // Construct from a std::array of field-descriptor-like objects (size must match N)
    template <typename FieldT>
        requires IsKeyValuePair<KeyType, ValueType, FieldT>
    constexpr explicit SortedHashArrayMap(const std::array<FieldT, N>& arr) {
        fillFromRange(arr.begin(), arr.end());
        sortFields();
    }

    // Construct from a C-style array: foo arr[M]
    template <typename FieldT>
        requires IsKeyValuePair<KeyType, ValueType, FieldT>
    constexpr explicit SortedHashArrayMap(const FieldT (&arr)[N]) {
        fillFromRange(std::begin(arr), std::end(arr));
        sortFields();
    }

    /// @brief 指定キーに対応するフィールドを探索する。
    /// @param key 探索するキー名。
    /// @return 見つかった場合はフィールドの元インデックス、未検出時はstd::nullopt。
    template <typename Lookup>
    std::optional<std::size_t> findIndex(const Lookup& key) const {
        return Algorithms::findIndex(std::span<const FieldInfo>(sortedFields_.data(), N), key);
    }

    /// @brief 指定キーに対応する値を取得する。見つからなければ nullptr を返す。
    template <typename Lookup>
    const ValueType* findValue(const Lookup& key) const {
        return Algorithms::findValue(std::span<const FieldInfo>(sortedFields_.data(), N), key);
    }

private:
    /// @brief フィールド情報を初期化する。
    /// @tparam Fields フィールドの型パラメータパック。
    /// @param fields フィールド定義群。
    /// @note フィールド情報を配列に詰めて、ハッシュ値でソートする。
    template <typename Iter>
        requires IsKeyValuePair<KeyType, ValueType, std::remove_cvref_t<decltype(*std::declval<Iter>())>>
    constexpr void fillFromRange(Iter begin, Iter end) {
        std::size_t i = 0;
        for (; begin != end; ++begin, ++i) {
            const auto& f = *begin;
            sortedFields_[i] = {
                static_cast<KeyType>(f.first),
                static_cast<ValueType>(f.second),
                Hash{}(f.first),
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
    using Array = std::array<FieldInfo, N>;
    using iterator = const FieldInfo*;
    using value_type = FieldInfo;
    iterator begin() const { return sortedFields_.data(); }
    iterator end() const { return sortedFields_.data() + N; }
private:
    Array sortedFields_{}; ///< ハッシュ順に整列したフィールド情報。
};

template <typename First, typename... Fields>
inline constexpr auto makeSortedHashArrayMap(First first, Fields... pairs) {
    using Key = typename First::first_type;
    using Value = typename First::second_type;
    return rai::collection::SortedHashArrayMap<Key, Value, sizeof...(pairs) + 1>(first, pairs...);
}

}  // namespace rai::collection
