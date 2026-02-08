// @file JsonFieldSet.cppm
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
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
export module rai.json.json_field_set;

namespace rai::json {

// ******************************************************************************** 基底インターフェース

/// @brief JSONフィールドセットの型消去用インターフェース。
/// @note 仮想関数の戻り値型として使用可能にするための基底クラス。
export class IJsonFieldSet {
public:
    virtual ~IJsonFieldSet() = default;

    /// @brief オブジェクトのフィールドのみを書き出す（startObject/endObjectなし）。
    /// @param writer 書き込み先のJsonWriter。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @note ポリモーフィック型の書き出し時に使用する。
    virtual void writeFieldsOnly(JsonWriter& writer, const void* obj) const = 0;

    /// @brief オブジェクト全体の読み込みを行う。
    /// @param parser 読み取り元の JsonParser
    /// @param obj 対象オブジェクトの void* ポインタ
    /// @note startObject/endObject は呼び出し側で処理してください。
    virtual void readObject(JsonParser& parser, void* obj) const = 0;
};

// ******************************************************************************** フィールドセット

/// @brief JSONフィールドセットの実装クラス。
/// @tparam Owner 所有者型。
/// @tparam Fields フィールド型のパラメータパック。
export template <typename Owner, typename... Fields>
class JsonFieldSetBody : public IJsonFieldSet {
private:
    // static_assertをメンバー関数に移動して遅延評価させる
    static constexpr void validateFields() {
        static_assert((std::is_base_of_v<typename std::remove_cvref_t<Fields>::OwnerType, Owner> && ...),
            "JsonFieldSetBody fields must be accessible from Owner type");
    }

public:
    constexpr explicit JsonFieldSetBody(Fields... fields)
        : fields_(std::move(fields)...) {
        validateFields();

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

    static constexpr std::size_t fieldCount() {
        return sizeof...(Fields);
    }

    /// @brief フィールド数を返す。
    /// @return 保持しているフィールド数。
    constexpr std::size_t size() const {
        return N_;
    }

    // ******************************************************************************** 書き出し
    /// @brief オブジェクトのフィールドのみを書き出す（startObject/endObjectなし）。
    /// @param writer 書き込み先のJsonWriter。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @note ポリモーフィック型の書き出し時に使用する。
    void writeFieldsOnly(JsonWriter& writer, const void* obj) const override {
        const Owner* owner = static_cast<const Owner*>(obj);
        forEachField([&](std::size_t, const auto& field) {
            const auto& value = owner->*(field.member);
            // デフォルトの toJson に委譲（明示的な分岐不要）
            field.writeKeyValue(writer, value);
        });
    }

    // ******************************************************************************** 読み込み
private:
    /// @brief オブジェクトのフィールドをJSONから読み込む（startObject/endObject済み）。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param obj 対象オブジェクト。
    /// @note JsonFieldSetBody内でフィールド探索と読み込みを行う。
    void readObjectFields(JsonParser& parser, Owner& obj) const {
        constexpr std::size_t N = sizeof...(Fields);
        std::bitset<N> seen{};
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
                throw std::runtime_error(std::string("JsonParser: duplicate key '") + k + "'");
            }
            seen[fieldIndex] = true;

            // テンプレート展開により各フィールドの型に応じた処理が静的に解決される
            visitField(fieldIndex, [&](const auto& field) {
                // デフォルトの read に委譲（明示的な分岐不要）
                obj.*(field.member) = field.read(parser);
            });
        }

        // 必須フィールドのチェックと既定値の反映
        forEachField([&](std::size_t index, const auto& field) {
            if (seen[index]) {
                return; // 読み込み済み。
            }
            field.applyMissing(obj.*(field.member));
        });
    }

    // ************************************************************************** JSONフィールド操作
public:
    /// @brief オブジェクト全体の読み込み（startObject/endObjectは呼び出し側が処理）。
    void readObject(JsonParser& parser, void* obj) const override {
        Owner* owner = static_cast<Owner*>(obj);
        readObjectFields(parser, *owner);
    }

private:
    /// @brief 指定インデックスのフィールドにアクセスする。
    /// @param index 対象フィールドの元インデックス。
    /// @param visitor フィールドを受け取るファンクタ。引数にJsonField&を取る必要がある。
    template <typename Visitor>
    void visitField(std::size_t index, Visitor&& visitor) const {
        if (index >= N_) {
            throw std::out_of_range("JsonFieldSetBody::visitField index out of range");
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
            throw std::out_of_range("JsonFieldSetBody::visitField index out of range");
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

    // Use std::string_view for keys so lookups using std::string_view work
    // reliably without the SortedHashArrayMap needing string_view-specific code.
    collection::SortedHashArrayMap<std::string_view, bool, N_> fieldMap_{}; ///< ハッシュ順に整列したフィールド情報。
    std::tuple<std::remove_cvref_t<Fields>...> fields_{}; ///< フィールド定義群。
};

export template <typename Owner, typename... Fields>
using JsonFieldSet = JsonFieldSetBody<Owner, Fields...>;

// ******************************************************************************** ヘルパー関数用のメタプログラミング型特性

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
    static_assert(!std::is_same_v<Promoted, void>, "JsonField owner types are not compatible");
    using type = typename DeduceOwner<Promoted, Rest...>::type;
};

// ******************************************************************************** ヘルパー関数

/// @brief JsonFieldSetを生成するヘルパー関数（所有者型を明示指定）。
/// @tparam Owner 所有者型。
/// @tparam Fields フィールド型のパラメータパック。
/// @param fields フィールド定義群。
/// @return 生成されたJsonFieldSet。
export template <typename Owner, typename... Fields>
constexpr auto makeJsonFieldSet(Fields... fields) {
    return JsonFieldSet<Owner, std::remove_cvref_t<Fields>...>(std::move(fields)...);
}

/// @brief JsonFieldSetを生成するヘルパー関数（所有者型を自動推論）。
/// @tparam Fields フィールド型のパラメータパック。
/// @param fields フィールド定義群。
/// @return 生成されたJsonFieldSet。
/// @note フィールドから所有者型を自動的に推論する。
export template <typename... Fields>
constexpr auto makeJsonFieldSet(Fields... fields) {
    static_assert(sizeof...(Fields) > 0, "makeJsonFieldSet requires explicit Owner when no fields are specified");
    using Owner = typename DeduceOwner<typename std::remove_cvref_t<Fields>::OwnerType...>::type;
    return makeJsonFieldSet<Owner>(std::move(fields)...);
}

}  // namespace rai::json
