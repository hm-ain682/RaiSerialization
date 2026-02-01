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
import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
export module rai.json.json_field_set;

namespace rai::json {

// ******************************************************************************** 基底インターフェース

/// @brief JSONフィールドセットの型消去用インターフェース。
/// @note 仮想関数の戻り値型として使用可能にするための基底クラス。
export class JsonFieldSetBase {
public:
    virtual ~JsonFieldSetBase() = default;

    /// @brief オブジェクトのフィールドのみを書き出す（startObject/endObjectなし）。
    /// @param writer 書き込み先のJsonWriter。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @note ポリモーフィック型の書き出し時に使用する。
    virtual void writeFieldsOnly(JsonWriter& writer, const void* obj) const = 0;

    /// @brief オブジェクト全体の読み込みを行う（startObject/endObject を含む）。
    /// @param parser 読み取り元の JsonParser
    /// @param obj 対象オブジェクトの void* ポインタ
    virtual void readObject(JsonParser& parser, void* obj) const = 0;

    /// @brief JSONキーに対応するフィールドを読み込む（startObject/endObjectなし）。
    /// @param parser 読み取り元のJsonParserオブジェクト。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @param key 読み込むフィールドのキー名。
    /// @return フィールドが見つかって読み込まれた場合はtrue、見つからない場合はfalse。
    /// @note ポリモーフィック型の読み込み時に使用する。
    virtual bool readFieldByKey(JsonParser& parser, void* obj, std::string_view key) const = 0;
};

export using IJsonFieldSet = JsonFieldSetBase;

// ******************************************************************************** フィールドセット

/// @brief JSONフィールドセットの実装クラス。
/// @tparam Owner 所有者型。
/// @tparam Fields フィールド型のパラメータパック。
export template <typename Owner, typename... Fields>
class JsonFieldSetBody : public JsonFieldSetBase {
private:
    // static_assertをメンバー関数に移動して遅延評価させる
    static constexpr void validateFields() {
        static_assert((std::is_base_of_v<typename std::remove_cvref_t<Fields>::OwnerType, Owner> && ...),
            "JsonFieldSetBody fields must be accessible from Owner type");
    }

public:
    using FieldTupleType = std::tuple<std::remove_cvref_t<Fields>...>;

    constexpr explicit JsonFieldSetBody(Fields... fields)
        : fields_(std::move(fields)...) {
        validateFields();

        // Build a small array of key/value descriptors from the stored fields_
        // so SortedHashArrayMap can be constructed from elements that have
        // .key and .value members (we map JsonField::isRequired() -> value here).
        using KV = std::pair<std::string_view, bool>;
        auto buildArr = [&]<std::size_t... I>(std::index_sequence<I...>) {
            std::array<KV, N_> arr{};
            ((arr[I] = KV{ std::get<I>(fields_).key, std::get<I>(fields_).isRequired() }), ...);
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

            // 指定された値と等しい場合は書き出しを省略
            if (field.shouldSkipWrite(value)) {
                return; // continue
            }

            writer.key(field.key);
            using FieldType = std::remove_cvref_t<decltype(field)>;

            // デフォルトの toJson に委譲（明示的な分岐不要）
            field.write(writer, value);
        });
    }

    // ******************************************************************************** 読み込み
private:
    /// @brief オブジェクトのフィールドをJSONから読み込む（startObject/endObject済み）。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param obj 対象オブジェクト。
    /// @note jsonFields()が返すJsonFieldSetBaseのreadFieldByKeyメソッドを使用する。
    void readObjectFields(JsonParser& parser, Owner& obj) const {
        constexpr std::size_t N = sizeof...(Fields);
        std::bitset<N> seen{};
        while (!parser.nextIsEndObject()) {
            std::string k = parser.nextKey();
            auto foundIndex = findFieldIndex(k);
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
            if (field.isRequired()) {
                throw std::runtime_error(
                    std::string("JsonParser: missing required key '") + field.key + "'");
            }
            if (field.hasDefault()) {
                auto v = field.makeDefault();
                obj.*(field.member) = std::move(v);
            }
        });
    }

    // ************************************************************************** JSONフィールド操作
public:
    /// @brief JSONキーに対応するフィールドを読み込む。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @param key 読み込むフィールドのキー名。
    /// @return フィールドが見つかって読み込まれた場合はtrue、見つからない場合はfalse。
    /// @note ポリモーフィック型の読み込み時に使用する。
    /// @brief オブジェクト全体の読み込み（startObject/endObjectを含む）。
    void readObject(JsonParser& parser, void* obj) const override {
        Owner* owner = static_cast<Owner*>(obj);
        parser.startObject();
        readObjectFields(parser, *owner);
        parser.endObject();
    }

    bool readFieldByKey(JsonParser& parser, void* obj, std::string_view key) const override {
        Owner* owner = static_cast<Owner*>(obj);
        auto foundIndex = findFieldIndex(key);
        if (!foundIndex) {
            return false;
        }

        const std::size_t fieldIndex = *foundIndex;
        visitField(fieldIndex, [&](const auto& field) {
            using FieldType = std::remove_cvref_t<decltype(field)>;
            using ValueType = typename FieldType::ValueType;

            // デフォルトの fromJson に委譲（明示的な分岐不要）
            owner->*(field.member) = field.read(parser);
        });

        return true;
    }

private:
    /// @brief 指定インデックスが必須かどうかを判定する。
    /// @param index フィールドの元インデックス。
    /// @return 必須フィールドならtrue。
    bool isFieldRequired(std::size_t index) const {
        bool result = false;
        visitField(index, [&](const auto& field) { result = field.isRequired(); });
        return result;
    }

    /// @brief 指定キーに対応するフィールドを探索する。
    /// @param key 探索するキー名。
    /// @return 見つかった場合はフィールドの元インデックス、未検出時はstd::nullopt。
    std::optional<std::size_t> findFieldIndex(std::string_view key) const {
        return fieldMap_.findIndex(key);
    }

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
    FieldTupleType fields_{}; ///< フィールド定義群。
};

export template <typename Owner, typename... Fields>
using JsonFieldSet = JsonFieldSetBody<Owner, Fields...>;

// ******************************************************************************** ヘルパー関数用のメタプログラミング型特性

/// @brief 2つの所有者型の上位型を推論する。
/// @tparam Left 左辺の型。
/// @tparam Right 右辺の型。
template <typename Left, typename Right>
struct PromoteOwnerType {
    using type = std::conditional_t<
        std::is_base_of_v<Left, Right>, Right,
        std::conditional_t<std::is_base_of_v<Right, Left>, Left, void>>;
};

/// @brief 複数の所有者型から共通の所有者型を推論する。
/// @tparam Owners 所有者型のパラメータパック。
template <typename... Owners>
struct DeduceOwnerType;

template <typename Owner>
struct DeduceOwnerType<Owner> {
    using type = Owner;
};

template <typename Owner, typename Next, typename... Rest>
struct DeduceOwnerType<Owner, Next, Rest...> {
    using Promoted = typename PromoteOwnerType<Owner, Next>::type;
    static_assert(!std::is_same_v<Promoted, void>, "JsonField owner types are not compatible");
    using type = typename DeduceOwnerType<Promoted, Rest...>::type;
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
    using Owner = typename DeduceOwnerType<typename std::remove_cvref_t<Fields>::OwnerType...>::type;
    return makeJsonFieldSet<Owner>(std::move(fields)...);
}

// ******************************************************************************** トップレベル読み書きヘルパー関数

/// @brief オブジェクトをJSON形式で書き出す（startObject/endObject含む）。
/// @tparam T HasJsonFieldsを実装している型。
/// @param writer 書き込み先のJsonWriter。
/// @param obj 書き出す対象のオブジェクト。
/// @note トップレベルのJSON書き出し用のヘルパー関数。
export template <HasJsonFields T>
void writeJsonObject(JsonWriter& writer, const T& obj) {
    auto& fields = obj.jsonFields();
    writer.startObject();
    fields.writeFieldsOnly(writer, &obj);
    writer.endObject();
}

/// @brief オブジェクトをJSONから読み込む（startObject/endObject含む）。
/// @tparam T HasJsonFieldsを実装している型。
/// @param parser 読み取り元のJsonParser互換オブジェクト。
/// @param obj 読み込み先のオブジェクト。
/// @note トップレベルのJSON読み込み用のヘルパー関数。
export template <HasJsonFields T>
void readJsonObject(JsonParser& parser, T& obj) {
    auto& fields = obj.jsonFields();
    // Delegate full object parsing (including defaults and required checks)
    fields.readObject(parser, &obj);
}

}  // namespace rai::json
