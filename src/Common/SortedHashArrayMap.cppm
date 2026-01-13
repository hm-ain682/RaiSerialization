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
    std::size_t hash = 0;       ///< キーから計算したハッシュ値（Hash の結果）。
    std::size_t originalIndex = 0; ///< 元のフィールド定義順序。

    /// @brief 引数付きコンストラクタ。
    /// @param k キー。
    /// @param v 値。
    /// @param h ハッシュ値。
    /// @param idx 元インデックス。
    constexpr MapEntry(KeyType k, ValueType v, std::size_t h, std::size_t idx)
        : key(std::move(k)), value(std::move(v)), hash(h), originalIndex(idx) {}

    /// @brief デフォルトコンストラクタ（KeyTypeがデフォルト構築可能な場合のみ）。
    constexpr MapEntry() requires std::is_default_constructible_v<KeyType> = default;
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

    /// @brief 指定キーに対応するエントリを検索する。
    /// @param entries エントリ配列のspan。
    /// @param key 検索キー。
    /// @return 見つかった場合はそのイテレータ、未検出時はentries.end()。
    template <typename Lookup>
    static typename std::span<const Entry>::iterator find(
        std::span<const Entry> entries, const Lookup& key) {
        const auto hash = Hash{}(key);

        const auto lower = std::lower_bound(
            entries.begin(),
            entries.end(),
            hash,
            [](const Entry& entry, std::size_t hashValue) {
                return entry.hash < hashValue;
            });

        // 同一ハッシュのエントリを線形探索してキー一致を確認
        for (auto it = lower; it != entries.end() && it->hash == hash; ++it) {
            if (KeyEqual{}(it->key, key)) {
                return it;
            }
        }
        return entries.end();
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
        auto it = Algorithms::find(entries_, key);
        if (it != entries_.end()) {
            return it->originalIndex;
        }
        return std::nullopt;
    }

    /// @brief 指定キーに対応する値を検索する。
    /// @tparam Lookup 検索キーの型。
    /// @param key 検索キー。
    /// @return 見つかった場合は値へのポインタ、未検出時はnullptr。
    template <typename Lookup>
    const ValueType* findValue(const Lookup& key) const {
        auto it = Algorithms::find(entries_, key);
        if (it != entries_.end()) {
            return &it->value;
        }
        return nullptr;
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
    using Array = FieldInfoArrayType;
    using Algorithms = SortedHashArrayMapAlgorithms<KeyType, ValueType, Traits>;

    /// @brief デフォルトコンストラクタ（KeyTypeがデフォルト構築可能な場合のみ）。
    constexpr SortedHashArrayMap() requires std::is_default_constructible_v<KeyType> = default;

    /// @brief フィールド情報を受け取るテンプレートコンストラクタ。
    /// @tparam Fields フィールド型のパラメータパック。
    /// @param fields フィールド定義群。要素：pair<KeyType, ValueType>型。
    /// @note フィールド情報を配列に詰めて、ハッシュ値でソートする。
    template <typename... Fields>
        requires (IsKeyValuePair<KeyType, ValueType, Fields> && ...)
    constexpr explicit SortedHashArrayMap(const Fields&... fields)
        : sortedFields_(makeFieldInfoArrayFromPack(
            std::make_index_sequence<N>{}, fields...)) {
        static_assert(sizeof...(Fields) == N, "SortedHashArrayMap: number of fields must equal N");
        sortFields();
    }

    // Construct from a std::array of field-descriptor-like objects (size must match N)
    template <typename FieldT>
        requires IsKeyValuePair<KeyType, ValueType, FieldT>
    constexpr explicit SortedHashArrayMap(const std::array<FieldT, N>& arr)
        : sortedFields_(makeFieldInfoArray(arr, std::make_index_sequence<N>{})) {
        sortFields();
    }

    // Construct from a C-style array: foo arr[M]
    template <typename FieldT>
        requires IsKeyValuePair<KeyType, ValueType, FieldT>
    constexpr explicit SortedHashArrayMap(const FieldT (&arr)[N])
        : sortedFields_(makeFieldInfoArrayFromCArray(arr, std::make_index_sequence<N>{})) {
        sortFields();
    }

    /// @brief 指定キーに対応するフィールドを探索する。
    /// @param key 探索するキー名。
    /// @return 見つかった場合はフィールドの元インデックス、未検出時はstd::nullopt。
    template <typename Lookup>
    std::optional<std::size_t> findIndex(const Lookup& key) const {
        std::span<const FieldInfo> entries(sortedFields_.data(), N);
        auto it = Algorithms::find(entries, key);
        if (it != entries.end()) {
            return it->originalIndex;
        }
        return std::nullopt;
    }

    /// @brief 指定キーに対応する値を取得する。見つからなければ nullptr を返す。
    template <typename Lookup>
    const ValueType* findValue(const Lookup& key) const {
        std::span<const FieldInfo> entries(sortedFields_.data(), N);
        auto it = Algorithms::find(entries, key);
        if (it != entries.end()) {
            return &it->value;
        }
        return nullptr;
    }

private:
    /// @brief std::arrayからFieldInfo配列を構築する。
    /// @tparam FieldT フィールドの型。
    /// @tparam Is インデックスシーケンス。
    /// @param arr ソース配列。
    /// @return FieldInfo配列。
    template <typename FieldT, std::size_t... Is>
    static constexpr Array makeFieldInfoArray(
        const std::array<FieldT, N>& arr, std::index_sequence<Is...>) {
        return Array{{
            FieldInfo{
                static_cast<KeyType>(arr[Is].first),
                static_cast<ValueType>(arr[Is].second),
                Hash{}(arr[Is].first),
                Is
            }...
        }};
    }

    /// @brief C配列からFieldInfo配列を構築する。
    /// @tparam FieldT フィールドの型。
    /// @tparam Is インデックスシーケンス。
    /// @param arr ソース配列。
    /// @return FieldInfo配列。
    template <typename FieldT, std::size_t... Is>
    static constexpr Array makeFieldInfoArrayFromCArray(
        const FieldT (&arr)[N], std::index_sequence<Is...>) {
        return Array{{
            FieldInfo{
                static_cast<KeyType>(arr[Is].first),
                static_cast<ValueType>(arr[Is].second),
                Hash{}(arr[Is].first),
                Is
            }...
        }};
    }

    /// @brief パラメータパックからFieldInfo配列を構築する。
    /// @tparam Is インデックスシーケンス。
    /// @tparam Fields フィールド型のパラメータパック。
    /// @param fields フィールド定義群。
    /// @return FieldInfo配列。
    template <std::size_t... Is, typename... Fields>
    static constexpr Array makeFieldInfoArrayFromPack(
        std::index_sequence<Is...>, const Fields&... fields) {
        // 一度タプルに格納してインデックスでアクセス
        auto tuple = std::forward_as_tuple(fields...);
        return Array{{
            FieldInfo{
                static_cast<KeyType>(std::get<Is>(tuple).first),
                static_cast<ValueType>(std::get<Is>(tuple).second),
                Hash{}(std::get<Is>(tuple).first),
                Is
            }...
        }};
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

    Array sortedFields_; ///< ハッシュ順に整列したフィールド情報。

public:
    using iterator = const FieldInfo*;
    using value_type = FieldInfo;
    iterator begin() const { return sortedFields_.data(); }
    iterator end() const { return sortedFields_.data() + N; }
};

template <typename First, typename... Fields>
inline constexpr auto makeSortedHashArrayMap(First first, Fields... pairs) {
    using Key = typename First::first_type;
    using Value = typename First::second_type;
    return rai::collection::SortedHashArrayMap<Key, Value, sizeof...(pairs) + 1>(first, pairs...);
}

}  // namespace rai::collection
