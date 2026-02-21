// @file ObjectSerializer.cppm
// @brief JSONフィールドセットの定義。構造体とJSONの相互変換を提供する。

module;
#include <memory>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>
#include <bitset>
#include <functional>
#include <ranges>
#include <typeinfo>

import rai.collection.sorted_hash_array_map;
import rai.serialization.format_io;
import rai.serialization.token_manager;
export module rai.serialization.object_serializer;

namespace rai::serialization {

// ******************************************************************************** 基底インターフェース

/// @brief オブジェクトを永続化するクラス。
/// @note 仮想関数の戻り値型として使用可能にするための基底クラス。
export class ObjectSerializer {
public:
    virtual ~ObjectSerializer() = default;

    /// @brief オブジェクトのフィールドのみを書き出す（startObject/endObjectなし）。
    /// @param writer 書き込み先のFormatWriter。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @note ポリモーフィック型の書き出し時に使用する。
    virtual void writeFields(FormatWriter& writer, const void* obj) const = 0;

    /// @brief オブジェクトのフィールドを読み込む。
    /// @param parser 読み取り元の FormatReader
    /// @param obj 対象オブジェクトの void* ポインタ
    /// @note startObject/endObject は呼び出し側で処理してください。
    virtual void readFields(FormatReader& parser, void* obj) const = 0;
};

// ******************************************************************************** フィールド集合による永続化

/// @brief FieldSerializer互換のインターフェースを満たすか判定するconcept。
/// @tparam Field 判定対象のフィールド型
export template <typename Field>
concept IsReadWriteField = requires(const Field& field, FormatReader& parser, FormatWriter& writer,
    typename Field::Owner& owner, const typename Field::Owner& constOwner) {
    { field.read(parser, owner) } -> std::same_as<void>;
    { field.write(writer, constOwner) } -> std::same_as<void>;
    { field.applyMissing(owner) } -> std::same_as<void>;
    { field.key } -> std::convertible_to<const char*>;
};

/// @brief フィールド集合による永続化クラス。
/// @tparam Owner 所有者型。
/// @tparam Fields フィールド型のパラメータパック。
export template <typename Owner, typename... Fields>
class FieldsObjectSerializer : public ObjectSerializer {
private:
    static_assert((IsReadWriteField<std::remove_cvref_t<Fields>> && ...),
        "FieldsObjectSerializer fields must satisfy FieldSerializer-like interface");
    static_assert((std::is_base_of_v<typename std::remove_cvref_t<Fields>::Owner, Owner> && ...),
        "FieldsObjectSerializer fields must be accessible from Owner type");

public:
    constexpr explicit FieldsObjectSerializer(Fields... fields)
        : fields_(std::move(fields)...) {
        // Build a small array of key/value descriptors from the stored fields_
        // so SortedHashArrayMap can be constructed from elements that have
        // .key and .value members (valueは探索用のダミーで未使用)。
        using KV = std::pair<std::string_view, bool>;
        auto buildArr = [&]<std::size_t... I>(std::index_sequence<I...>) {
            std::array<KV, N_> arr{};
            ((arr[I] = KV{ std::get<I>(fields_).key, false }), ...);
            return arr;
        };

        auto arr = buildArr(std::make_index_sequence<N_>{});
        fieldMap_ = collection::SortedHashArrayMap<std::string_view, bool, N_>(arr);
    }

    /// @brief フィールド数を返す。
    /// @return 保持しているフィールド数。
    constexpr std::size_t size() const {
        return N_;
    }

    /// @brief オブジェクトのフィールドのみを書き出す（startObject/endObjectなし）。
    /// @param writer 書き込み先のFormatWriter。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @note ポリモーフィック型の書き出し時に使用する。
    void writeFields(FormatWriter& writer, const void* obj) const override {
        const Owner* owner = static_cast<const Owner*>(obj);
        forEachField([&](std::size_t, const auto& field) {
            // デフォルトの toJson に委譲（明示的な分岐不要）
            field.write(writer, *owner);
        });
    }

    /// @brief オブジェクトのフィールドをJSONから読み込む（startObject/endObjectなし）。
    /// @param parser 読み取り元のFormatReader互換オブジェクト。
    /// @param obj 対象オブジェクト。
    /// @note FieldsObjectSerializer内でフィールド探索と読み込みを行う。
    void readFields(FormatReader& parser, void* obj) const override {
        auto& owner = *static_cast<Owner*>(obj);
        std::bitset<N_> seen{};
        while (!parser.nextIsEndObject()) {
            std::string k = parser.nextKey();
            auto foundIndex = fieldMap_.findIndex(k);
            if (!foundIndex) {
                parser.noteUnknownKey(k);
                parser.skipValue();
                continue;
            }
            const std::size_t fieldIndex = *foundIndex;
            if (seen[fieldIndex]) {
                throw std::runtime_error(std::string("Parser: duplicate key '") + k + "'");
            }
            seen[fieldIndex] = true;

            // テンプレート展開により各フィールドの型に応じた処理が静的に解決される
            visitField(fieldIndex, [&](const auto& field) {
                field.read(parser, owner);
            });
        }

        // 必須フィールドのチェックと既定値の反映
        forEachField([&](std::size_t index, const auto& field) {
            if (!seen[index]) {
                field.applyMissing(owner);
            }
        });
    }

    // ************************************************************************** JSONフィールド操作

private:
    /// @brief 指定インデックスのフィールドにアクセスする。
    /// @param index 対象フィールドの元インデックス。
    /// @param visitor フィールドを受け取るファンクタ。引数にFieldSerializer&を取る必要がある。
    template <typename Visitor>
    void visitField(std::size_t index, Visitor&& visitor) const {
        if (index >= N_) {
            throw std::out_of_range("FieldsObjectSerializer::visitField index out of range");
        }
        visitFieldImpl(index, std::forward<Visitor>(visitor), std::make_index_sequence<N_>{});
    }

    template <typename Visitor, std::size_t... Index>
    void visitFieldImpl(std::size_t index, Visitor&& visitor, std::index_sequence<Index...>) const {
        bool matched =
            ((index == Index ? (std::forward<Visitor>(visitor)(std::get<Index>(fields_)), true)
                             : false) ||
             ...);
        if (!matched) {
            throw std::out_of_range("FieldsObjectSerializer::visitField index out of range");
        }
    }

    /// @brief すべてのフィールドを列挙する。
    /// @param visitor インデックスとフィールドを受け取るファンクタ。
    template <typename Visitor>
    void forEachField(Visitor&& visitor) const {
        forEachFieldImpl(std::forward<Visitor>(visitor), std::make_index_sequence<N_>{});
    }

    template <typename Visitor, std::size_t... Index>
    void forEachFieldImpl(Visitor&& visitor, std::index_sequence<Index...>) const {
        (visitor(Index, std::get<Index>(fields_)), ...);
    }

    // ******************************************************************************** フィールド
    static constexpr std::size_t N_ = sizeof...(Fields);

    ///! jsonキーに対応するフィールド検索用。
    collection::SortedHashArrayMap<std::string_view, bool, N_> fieldMap_{};
    std::tuple<std::remove_cvref_t<Fields>...> fields_{}; ///< フィールド定義群。
};

// ******************************************************************************** ヘルパー関数

/// @brief 2つの所有者型の上位型を推論する。
/// @tparam Left 左辺の型。
/// @tparam Right 右辺の型。
template <typename Left, typename Right>
struct PromoteOwner {
    using type = std::conditional_t<
        std::is_base_of_v<Left, Right>, Right,
        std::conditional_t<std::is_base_of_v<Right, Left>, Left, void>>;
};

/// @brief 複数の所有者型から共通の所有者型を推論する。
/// @tparam Owners 所有者型のパラメータパック。
template <typename... Owners>
struct DeduceOwner;

template <typename Owner>
struct DeduceOwner<Owner> {
    using type = Owner;
};

template <typename Owner, typename Next, typename... Rest>
struct DeduceOwner<Owner, Next, Rest...> {
    using Promoted = typename PromoteOwner<Owner, Next>::type;
    static_assert(!std::is_same_v<Promoted, void>, "FieldSerializer owner types are not compatible");
    using type = typename DeduceOwner<Promoted, Rest...>::type;
};

/// @brief FieldsObjectSerializerを生成するヘルパー関数（所有者型を自動推論）。
/// @tparam Fields フィールド型のパラメータパック。
/// @param fields フィールド定義群。
/// @return 生成されたFieldsObjectSerializer。
/// @note フィールドから所有者型を自動的に推論する。
export template <typename... Fields>
constexpr auto getFieldSet(Fields... fields) {
    static_assert(sizeof...(Fields) > 0, "getFieldSet requires at least one field");
    using Owner = typename DeduceOwner<typename std::remove_cvref_t<Fields>::Owner...>::type;
    return FieldsObjectSerializer<Owner, std::remove_cvref_t<Fields>...>(std::move(fields)...);
}

}  // namespace rai::serialization
