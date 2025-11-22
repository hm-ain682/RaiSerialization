// @file JsonBinding.cppm
// @brief JSONバインディングの定義。構造体とJSONの相互変換を提供する。

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

import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
import rai.json.sorted_field_map;
import rai.json.enum_converter;
export module rai.json.json_binding;

namespace rai::json {

// ******************************************************************************** 概念定義
// json入出力対象型は以下の通り。
// ・プリミティブ型（int, double, bool など）
// ・std::string
// ・vector, unique_ptr, variant；要素がjson入出力対象型であること。
// ・jsonFields()を持つ型→HasJsonFields

// 文字列をJSONとして読み込む場合に用いる。文字列入力部分は並列化する必要がないため。
export using ActiveJsonParser = JsonParser<JsonTokenManager>;

/// @brief プリミティブ型かどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsFundamentalValue = std::is_fundamental_v<T>;

/// @brief jsonFields()メンバー関数を持つかどうかを判定するconcept。
/// @tparam T 判定対象の型。
export template <typename T>
concept HasJsonFields = requires(const T& t) { t.jsonFields(); };

/// @brief toJsonメンバー関数を持つかどうかを判定するconcept。
/// @tparam Field フィールド型。
/// @tparam ValueType フィールドが管理する値の型。
template <typename Field, typename ValueType>
concept HasToJson = requires(const Field& field, const ValueType& value) {
    { field.toJson(value) } -> std::convertible_to<std::string>;
};

/// @brief fromJsonメンバー関数を持つかどうかを判定するconcept。
/// @tparam Field フィールド型。
/// @tparam ValueType フィールドが管理する値の型。
template <typename Field, typename ValueType>
concept HasFromJson = requires(const Field& field, const std::string& str) {
    { field.fromJson(str) } -> std::convertible_to<ValueType>;
};

/// @brief JsonPolymorphicFieldかどうかを判定するconcept。
/// @tparam Field フィールド型。
template <typename Field>
concept IsPolymorphicField = requires(const Field& field) {
    typename Field::BaseType;
    { field.getTypeName(std::declval<const typename Field::BaseType*>()) } -> std::convertible_to<std::string>;
    { field.findEntry(std::string_view{}) };
};

/// @brief writeJsonメソッドを持つ型を表すconcept。
/// @tparam T 型。
export template <typename T>
concept HasWriteJson = requires(const T& obj, JsonWriter& writer) {
    { obj.writeJson(writer) } -> std::same_as<void>;
};

/// @brief readJsonメソッドを持つ型を表すconcept。
/// @tparam T 型。
/// @tparam Parser JsonParser互換型。
export template <typename T, typename Parser>
concept HasReadJson = requires(T& obj, Parser& parser) {
    { obj.readJson(parser) } -> std::same_as<void>;
};

// ******************************************************************************** メタプログラミング用の型特性

/// @brief メンバーポインタの特性を抽出するメタ関数。
/// @tparam T メンバーポインタ型。
template <typename T>
struct MemberPointerTraits;

template <typename Owner, typename Value>
struct MemberPointerTraits<Value Owner::*> {
    using OwnerType = Owner;
    using ValueType = Value;
};

// ******************************************************************************** 基底インターフェース

/// @brief JSONフィールドセットの型消去用インターフェース。
/// @note 仮想関数の戻り値型として使用可能にするための基底クラス。
/// @tparam Parser JsonParser互換型。
export template<typename Parser>
class JsonFieldSetBase {
public:
    virtual ~JsonFieldSetBase() = default;

    /// @brief オブジェクトのフィールドのみを書き出す（startObject/endObjectなし）。
    /// @param writer 書き込み先のJsonWriter。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @note ポリモーフィック型の書き出し時に使用する。
    virtual void writeFieldsOnly(JsonWriter& writer, const void* obj) const = 0;

    /// @brief JSONキーに対応するフィールドを読み込む（startObject/endObjectなし）。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @param key 読み込むフィールドのキー名。
    /// @return フィールドが見つかって読み込まれた場合はtrue、見つからない場合はfalse。
    /// @note ポリモーフィック型の読み込み時に使用する。
    virtual bool readFieldByKey(Parser& parser, void* obj, std::string_view key) const = 0;
};

export using IJsonFieldSet = JsonFieldSetBase<ActiveJsonParser>;

// ******************************************************************************** フィールド定義

/// @brief JSONフィールドの基本定義。
/// @tparam MemberPtr メンバー変数へのポインタ。
export template <auto MemberPtr>
struct JsonField {
    static_assert(std::is_member_object_pointer_v<decltype(MemberPtr)>, "JsonField requires a data member pointer");
    using Traits = MemberPointerTraits<decltype(MemberPtr)>;
    using OwnerType = typename Traits::OwnerType;
    using ValueType = typename Traits::ValueType;
    inline static constexpr auto member = MemberPtr;
    const char* key{};
    bool required{false};

    constexpr explicit JsonField(const char* keyName, bool req = false) : key(keyName), required(req) {}
};

/// @brief Enum型のフィールド用に特化したJsonField派生クラス。
/// @tparam MemberPtr Enumメンバー変数へのポインタ。
/// @tparam Entries Enumと文字列のマッピング配列への参照。
export template <auto MemberPtr, const auto& Entries>
struct JsonEnumField : JsonField<MemberPtr> {
    using Base = JsonField<MemberPtr>;
    using typename Base::ValueType;
    static_assert(std::is_enum_v<ValueType>, "JsonEnumField requires enum type");

    // エントリ数を配列サイズから自動推論
    static constexpr std::size_t N = std::extent_v<std::remove_reference_t<decltype(Entries)>>;

    /// @brief EnumConverterインスタンス。
    /// @note 各JsonEnumFieldインスタンスがコンバータを保持する。
    EnumConverter<ValueType, N> converter_{Entries};

    /// @brief Enum用フィールドのコンストラクタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    constexpr explicit JsonEnumField(const char* keyName, bool req = false)
        : Base(keyName, req) {}

    /// @brief Enum値を文字列に変換する。
    /// @param value 変換対象のenum値。
    /// @return JSON文字列。見つからない場合は例外を投げる。
    /// @note EnumConverterを使用して変換する。
    std::string toJson(const ValueType& value) const {
        auto result = converter_.toString(value);
        if (!result) {
            throw std::runtime_error("Failed to convert enum to string");
        }
        return std::string(*result);
    }

    /// @brief 文字列からEnum値に変換する。
    /// @param jsonValue JSON文字列。
    /// @return 変換されたenum値。見つからない場合は例外を投げる。
    /// @note EnumConverterを使用して高速検索する。
    ValueType fromJson(const std::string& jsonValue) const {
        auto result = converter_.fromString(jsonValue);
        if (!result) {
            throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
        }
        return *result;
    }
};

/// @brief 型名とファクトリ関数のマッピングエントリ。
/// @tparam BaseType 基底クラス型。
export template <typename BaseType>
struct PolymorphicTypeEntry {
    const char* typeName; ///< JSON上の型名。
    std::unique_ptr<BaseType> (*factory)(); ///< オブジェクト生成関数ポインタ。
};

/// @brief ポリモーフィック型（unique_ptr<基底クラス>）用のJsonField派生クラス。
/// @tparam MemberPtr unique_ptr<基底クラス>メンバー変数へのポインタ。
/// @tparam Entries 型名とファクトリ関数のマッピング配列への参照。
export template <auto MemberPtr, const auto& Entries>
struct JsonPolymorphicField : JsonField<MemberPtr> {
    using Base = JsonField<MemberPtr>;
    using typename Base::ValueType;

    // ValueTypeはstd::unique_ptr<T>であることを確認
    static_assert(std::is_same_v<ValueType, std::unique_ptr<typename ValueType::element_type>>,
                  "JsonPolymorphicField requires std::unique_ptr type");

    using BaseType = typename ValueType::element_type;

    // エントリ数を配列サイズから自動推論
    static constexpr std::size_t N = std::extent_v<std::remove_reference_t<decltype(Entries)>>;

    /// @brief ポリモーフィック型用フィールドのコンストラクタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    constexpr explicit JsonPolymorphicField(const char* keyName, bool req = false)
        : Base(keyName, req) {}

    /// @brief 型名から対応するエントリを検索する。
    /// @param typeName 検索する型名。
    /// @return 見つかった場合はエントリへのポインタ、見つからない場合はnullptr。
    const PolymorphicTypeEntry<BaseType>* findEntry(std::string_view typeName) const {
        for (std::size_t i = 0; i < N; ++i) {
            if (Entries[i].typeName == typeName) {
                return &Entries[i];
            }
        }
        return nullptr;
    }

    /// @brief オブジェクトから型名を取得する。
    /// @param obj 対象オブジェクトへのポインタ。
    /// @return 型名。見つからない場合は例外を投げる。
    std::string getTypeName(const BaseType* obj) const {
        if (!obj) {
            throw std::runtime_error("Cannot get type name from null pointer");
        }
        for (std::size_t i = 0; i < N; ++i) {
            auto testObj = Entries[i].factory();
            if (typeid(*obj) == typeid(*testObj)) {
                return Entries[i].typeName;
            }
        }
        throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(*obj).name());
    }

};

// ******************************************************************************** フィールドセット

/// @brief JSONフィールドセットの実装クラス。
/// @tparam Owner 所有者型。
/// @tparam Fields フィールド型のパラメータパック。
export template <typename Parser, typename Owner, typename... Fields>
class JsonFieldSetBody : public JsonFieldSetBase<Parser> {
private:
    using BaseType = JsonFieldSetBase<Parser>;

    // static_assertをメンバー関数に移動して遅延評価させる
    static constexpr void validateFields() {
        static_assert((std::is_base_of_v<typename std::remove_cvref_t<Fields>::OwnerType, Owner> && ...),
                      "JsonFieldSetBody fields must be accessible from Owner type");
    }

public:
    using OwnerType = Owner;
    using FieldTupleType = std::tuple<std::remove_cvref_t<Fields>...>;

    constexpr explicit JsonFieldSetBody(Fields... fields) : fields_(std::move(fields)...) {
        validateFields(); // ここで検証を実行
        fieldMap_.initialize(fields...);
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
            writer.key(field.key);
            const auto& value = owner->*(field.member);
            using FieldType = std::remove_cvref_t<decltype(field)>;

            // ポリモーフィックフィールドの場合は特別処理
            if constexpr (IsPolymorphicField<FieldType>) {
                writePolymorphicObject(writer, field, value);
            }
            // toJsonメンバーを持つ場合はそれを使用
            else if constexpr (HasToJson<FieldType, std::remove_cvref_t<decltype(value)>>) {
                std::string jsonValue = field.toJson(value);
                writer.writeObject(jsonValue);
            } else {
                // ここでエラーになる場合は、fieldの対象メンバー変数の型が、json入出力対象対象型ではない。
                writeObject(writer, value);
            }
        });
    }
private:
    /// @brief ポリモーフィック型（unique_ptr）を書き出す。
    /// @tparam FieldType JsonPolymorphicFieldの型。
    /// @tparam T unique_ptrの要素型。
    /// @param writer 書き込み先のJsonWriter。
    /// @param field ポリモーフィックフィールド。
    /// @param ptr 書き出す対象のunique_ptr。
    /// @note nullの場合はnullを書き出し、そうでない場合は型名とフィールドを含むオブジェクトを書き出す。
    template <typename FieldType, typename T>
    void writePolymorphicObject(JsonWriter& writer, const FieldType& field, const std::unique_ptr<T>& ptr) const {
        if (!ptr) {
            writer.null();
            return;
        }

        // "type"キーを含むオブジェクトとして書き出す
        writer.startObject();

        // 型名を取得して書き出す
        std::string typeName = field.getTypeName(ptr.get());
        writer.key("type");
        writer.writeObject(typeName);

        // 実際のオブジェクトのフィールドを書き出す
        if constexpr (HasJsonFields<T>) {
            auto& fields = ptr->jsonFields();
            fields.writeFieldsOnly(writer, ptr.get());
        }

        writer.endObject();
    }

    template<IsFundamentalValue T>
    void writeObject(JsonWriter& writer, T value) const {
        writer.writeObject(value);
    }

    void writeObject(JsonWriter& writer, const std::string& value) const {
        writer.writeObject(value);
    }

    // vectorなどの範囲型
    template <std::ranges::range Range>
    void writeObject(JsonWriter& writer, const Range& range) const {
        writer.startArray();
        for (const auto& item : range) {
            if constexpr (HasJsonFields<std::ranges::range_value_t<Range>>) {
                auto& fields = item.jsonFields();
                writer.startObject();
                fields.writeFieldsOnly(writer, &item);
                writer.endObject();
            } else {
                writeObject(writer, item);
            }
        }
        writer.endArray();
    }

    // unique_ptr型
    template <HasJsonFields T>
    void writeObject(JsonWriter& writer, const std::unique_ptr<T>& ptr) const {
        if (ptr) {
            auto& fields = ptr->jsonFields();
            writer.startObject();
            fields.writeFieldsOnly(writer, ptr.get());
            writer.endObject();
        } else {
            writer.null();
        }
    }

    // ポリモーフィック型（JsonPolymorphicFieldで管理されるunique_ptr）の書き込み
    template <typename FieldType, HasJsonFields T>
    void writePolymorphicObject(JsonWriter& writer, const FieldType& field, const std::unique_ptr<T>& ptr) const {
        if (!ptr) {
            writer.null();
            return;
        }

        // "type"キーを含むオブジェクトとして書き出す
        writer.startObject();

        // 型名を取得して書き出す
        std::string typeName = field.getTypeName(ptr.get());
        writer.key("type");
        writer.writeObject(typeName);

        // 実際のフィールド内容を書き出す
        auto& fields = ptr->jsonFields();
        fields.writeFieldsOnly(writer, ptr.get());

        writer.endObject();
    }

    // variant型の処理
    template <typename... Types>
    void writeObject(JsonWriter& writer, const std::variant<Types...>& var) const {
        std::visit([&](const auto& value) {
            using ValueType = std::decay_t<decltype(value)>;
            if constexpr (HasJsonFields<ValueType>) {
                auto& fields = value.jsonFields();
                writer.startObject();
                fields.writeFieldsOnly(writer, &value);
                writer.endObject();
            } else {
                writeObject(writer, value);
            }
        }, var);
    }

    // jsonFields()を実装している型
    template <HasJsonFields T>
    void writeObject(JsonWriter& writer, const T& obj) const {
        auto& fields = obj.jsonFields();
        writer.startObject();
        fields.writeFieldsOnly(writer, static_cast<const void*>(&obj));
        writer.endObject();
    }

    // カスタムJSON出力を持つ型（writeJsonメソッドを持つ型）
    template <HasWriteJson T>
    void writeObject(JsonWriter& writer, const T& obj) const {
        obj.writeJson(writer);
    }

    // ******************************************************************************** 読み込み
private:
    // jsonFields を持つ型への読み込み
    template <HasJsonFields T>
    void readObject(Parser& parser, T& out) const {
        auto& fields = out.jsonFields();
        parser.startObject();
        readObjectFieldsByFieldSet(parser, fields, &out);
        parser.endObject();
    }

    /// @brief オブジェクトのフィールドをJSONから読み込む（startObject/endObject済み）。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param obj 対象オブジェクト。
    /// @note jsonFields()が返すBaseTypeのreadFieldByKeyメソッドを使用する。
    void readObjectFields(Parser& parser, Owner& obj) const {
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
                using FieldType = std::remove_cvref_t<decltype(field)>;
                using ValueType = typename FieldType::ValueType;

                // ポリモーフィックフィールドの場合は特別処理
                if constexpr (IsPolymorphicField<FieldType>) {
                    readPolymorphicObject(parser, field, obj.*(field.member));
                }
                // fromJsonメンバーを持つ場合はそれを使用
                else if constexpr (HasFromJson<FieldType, ValueType>) {
                    std::string jsonValue;
                    parser.readTo(jsonValue);
                    obj.*(field.member) = field.fromJson(jsonValue);
                } else {
                    // ここでエラーになる場合は、fieldの対象メンバー変数の型が、json入出力対象対象型ではない。
                    readObject(parser, obj.*(field.member));
                }
            });
        }

        // 必須フィールドのチェック
        forEachField([&](std::size_t index, const auto& field) {
            if (field.required && !seen[index]) {
                throw std::runtime_error(
                    std::string("JsonParser: missing required key '") + field.key + "'");
            }
        });
    }

    template <IsFundamentalValue T>
    void readObject(Parser& parser, T& out) const {
        T temp = out;
        parser.readTo(temp);
        out = temp;
    }

    void readObject(Parser& parser, std::string& out) const {
        parser.readTo(out);
    }

    /// @brief ポリモーフィック型（unique_ptr）を読み込む。
    /// @tparam FieldType JsonPolymorphicFieldの型。
    /// @tparam T unique_ptrの要素型。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param field ポリモーフィックフィールド。
    /// @param out 読み込み先のunique_ptr。
    /// @note nullの場合はnullptrを設定し、そうでない場合は型名に基づいてオブジェクトを生成して読み込む。
    template <typename FieldType, typename T>
    void readPolymorphicObject(Parser& parser, const FieldType& field, std::unique_ptr<T>& out) const {
        if (parser.nextIsNull()) {
            parser.skipValue();
            out.reset();
            return;
        }

        parser.startObject();

        // "type"キーを読み込む
        std::string typeKey = parser.nextKey();
        if (typeKey != "type") {
            throw std::runtime_error(std::string("Expected 'type' key for polymorphic object, got '") + typeKey + "'");
        }

        std::string typeName;
        parser.readTo(typeName);

        // 型名からファクトリを検索してオブジェクトを生成
        const auto* entry = field.findEntry(typeName);
        if (!entry) {
            throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeName);
        }

        auto tmp = entry->factory();

        // オブジェクトのフィールドを読み込む
        // jsonFields()が返すFieldSetを使用してフィールドを読み込む
        if constexpr (HasJsonFields<T>) {
            auto& fields = tmp->jsonFields();

            // 残りのキーを読み込んでフィールドにマッピング
            readObjectFieldsByFieldSet(parser, fields, tmp.get());
        } else {
            // HasJsonFieldsを実装していない場合は、残りをスキップ
            while (!parser.nextIsEndObject()) {
                std::string k = parser.nextKey();
                parser.noteUnknownKey(k);
                parser.skipValue();
            }
        }

        parser.endObject();
        out = std::move(tmp);
    }

    /// @brief BaseTypeを使用してオブジェクトのフィールドを読み込む（startObject/endObject済み）。
    /// @tparam T オブジェクトの型。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param fields オブジェクトのフィールドセット。
    /// @param obj 読み込み先のオブジェクト。
    /// @note この関数は、オブジェクトが既にstartObjectされている状態を想定している。
    template <typename T>
    void readObjectFieldsByFieldSet(Parser& parser, const BaseType& fields, T* obj) const {
        while (!parser.nextIsEndObject()) {
            std::string k = parser.nextKey();
            if (!fields.readFieldByKey(parser, obj, k)) {
                parser.noteUnknownKey(k);
                parser.skipValue();
            }
        }
    }

    // unique_ptr<T>（null を許可）
    template <typename T>
    void readObject(Parser& parser, std::unique_ptr<T>& out) const {
        if (parser.nextIsNull()) {
            parser.skipValue();
            out.reset();
            return;
        }
        auto tmp = std::make_unique<T>();
        if constexpr (HasJsonFields<T>) {
            auto& fields = tmp->jsonFields();
            parser.startObject();
            readObjectFieldsByFieldSet(parser, fields, tmp.get());
            parser.endObject();
        } else {
            readObject(parser, *tmp);
        }
        out = std::move(tmp);
    }

    // カスタムJSON入力を持つ型（readJsonメソッドを持つ型）
    template <HasReadJson<Parser> T>
    void readObject(Parser& parser, T& out) const {
        out.readJson(parser);
    }

    // 配列
    template <typename T>
    void readObject(Parser& parser, std::vector<T>& out) const {
        parser.startArray();
        out.clear();
        while (!parser.nextIsEndArray()) {
            T elem{};
            if constexpr (HasJsonFields<T>) {
                auto& fields = elem.jsonFields();
                parser.startObject();
                readObjectFieldsByFieldSet(parser, fields, &elem);
                parser.endObject();
            } else {
                readObject(parser, elem);
            }
            out.push_back(std::move(elem));
        }
        parser.endArray();
    }

    // ************************************************************************** JSONフィールド操作
public:
    /// @brief JSONキーに対応するフィールドを読み込む。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @param key 読み込むフィールドのキー名。
    /// @return フィールドが見つかって読み込まれた場合はtrue、見つからない場合はfalse。
    /// @note ポリモーフィック型の読み込み時に使用する。
    bool readFieldByKey(Parser& parser, void* obj, std::string_view key) const override {
        Owner* owner = static_cast<Owner*>(obj);
        auto foundIndex = findFieldIndex(key);
        if (!foundIndex) {
            return false;
        }

        const std::size_t fieldIndex = *foundIndex;
        visitField(fieldIndex, [&](const auto& field) {
            using FieldType = std::remove_cvref_t<decltype(field)>;
            using ValueType = typename FieldType::ValueType;

            // ポリモーフィックフィールドの場合は特別処理
            if constexpr (IsPolymorphicField<FieldType>) {
                readPolymorphicObject(parser, field, owner->*(field.member));
            }
            // fromJsonメンバーを持つ場合はそれを使用
            else if constexpr (HasFromJson<FieldType, ValueType>) {
                std::string jsonValue;
                parser.readTo(jsonValue);
                owner->*(field.member) = field.fromJson(jsonValue);
            } else {
                readObject(parser, owner->*(field.member));
            }
        });

        return true;
    }

private:
    /// @brief 指定インデックスが必須かどうかを判定する。
    /// @param index フィールドの元インデックス。
    /// @return 必須フィールドならtrue。
    bool isFieldRequired(std::size_t index) const {
        bool result = false;
        visitField(index, [&](const auto& field) { result = field.required; });
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

    FieldTupleType fields_{}; ///< フィールド定義群。
    SortedFieldMap<const char*, bool, N_> fieldMap_{}; ///< ハッシュ順に整列したフィールド情報。
};

export template <typename Owner, typename... Fields>
using JsonFieldSet = JsonFieldSetBody<ActiveJsonParser, Owner, Fields...>;

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
void readJsonObject(ActiveJsonParser& parser, T& obj) {
    auto& fields = obj.jsonFields();
    parser.startObject();
    while (!parser.nextIsEndObject()) {
        std::string key = parser.nextKey();
        if (!fields.readFieldByKey(parser, &obj, key)) {
            parser.noteUnknownKey(key);
            parser.skipValue();
        }
    }
    parser.endObject();
}

}  // namespace rai::json
