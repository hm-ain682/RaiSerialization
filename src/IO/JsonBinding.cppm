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
export module rai.json.json_binding;

namespace rai::json {

// ******************************************************************************** 概念定義
// json入出力対象型は以下の通り。
// ・プリミティブ型（int, double, bool など）
// ・std::string
// ・vector, unique_ptr, variant；要素がjson入出力対象型であること。
// ・jsonFields()を持つ型→HasJsonFields

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
concept HasToJson = requires(const Field& field, JsonWriter& writer, const ValueType& value) {
    { field.toJson(writer, value) } -> std::same_as<void>;
};

/// @brief fromJsonメンバー関数を持つかどうかを判定するconcept。
/// @tparam Field フィールド型。
/// @tparam ValueType フィールドが管理する値の型。
/// @note 変更: 以前は std::string を受け取っていたが、JsonParser を直接受け取るようにする。
template <typename Field, typename ValueType>
concept HasFromJson = requires(const Field& field, JsonParser& parser) {
    { field.fromJson(parser) } -> std::convertible_to<ValueType>;
};

/// @brief JsonPolymorphicFieldかどうかを判定するconcept。
/// @tparam Field フィールド型。
template <typename Field>
concept IsPolymorphicField = requires(const Field& field) {
    typename Field::BaseType;
    { field.getTypeName(std::declval<const typename Field::BaseType&>()) } -> std::convertible_to<std::string>;
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
export template <typename T>
concept HasReadJson = requires(T& obj, JsonParser& parser) {
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
export class JsonFieldSetBase {
public:
    virtual ~JsonFieldSetBase() = default;

    /// @brief オブジェクトのフィールドのみを書き出す（startObject/endObjectなし）。
    /// @param writer 書き込み先のJsonWriter。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @note ポリモーフィック型の書き出し時に使用する。
    virtual void writeFieldsOnly(JsonWriter& writer, const void* obj) const = 0;

    /// @brief JSONキーに対応するフィールドを読み込む（startObject/endObjectなし）。
    /// @param parser 読み取り元のJsonParserオブジェクト。
    /// @param obj 対象オブジェクトのvoidポインタ。
    /// @param key 読み込むフィールドのキー名。
    /// @return フィールドが見つかって読み込まれた場合はtrue、見つからない場合はfalse。
    /// @note ポリモーフィック型の読み込み時に使用する。
    virtual bool readFieldByKey(JsonParser& parser, void* obj, std::string_view key) const = 0;
};

export using IJsonFieldSet = JsonFieldSetBase;

// ******************************************************************************** フィールド定義

/// @brief JSONフィールドの基本定義。
/// @tparam MemberPtr メンバー変数へのポインタ。
export template <typename MemberPtrType>
struct JsonField {
    static_assert(std::is_member_object_pointer_v<MemberPtrType>, "JsonField requires a data member pointer");
    using Traits = MemberPointerTraits<MemberPtrType>;
    using OwnerType = typename Traits::OwnerType;
    using ValueType = typename Traits::ValueType;
    MemberPtrType member{}; // pointer-to-member stored at runtime
    const char* key{};
    bool required{false};

    constexpr explicit JsonField(MemberPtrType memberPtr, const char* keyName, bool req = false)
        : member(memberPtr), key(keyName), required(req) {}
};

/// @brief Enumと文字列のマッピングエントリ。
/// @tparam EnumType 対象のenum型。
template <typename EnumType>
struct EnumEntry {
    EnumType value;      ///< Enum値。
    const char* name;    ///< 対応する文字列名。
};

/// @brief Enum型のフィールド用に特化したJsonField派生クラス。
/// @tparam MemberPtr Enumメンバー変数へのポインタ。
/// @tparam Entries Enumと文字列のマッピング配列への参照。
export template <typename MemberPtrType>
struct JsonEnumField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;
    static_assert(std::is_enum_v<ValueType>, "JsonEnumField requires enum type");

    // 実行時に渡されるエントリ配列への参照とサイズ
    const EnumEntry<ValueType>* entriesPtr_{nullptr};
    std::size_t entriesCount_{0};

    /// @brief Enum用フィールドのコンストラクタ（エントリ配列を受け取る）
    /// @param memberPtr ポインタメンバ
    /// @param keyName JSONキー名
    /// @param entries エントリ配列
    template <std::size_t N>
    constexpr explicit JsonEnumField(MemberPtrType memberPtr, const char* keyName, const EnumEntry<ValueType> (&entries)[N], bool req = false)
        : Base(memberPtr, keyName, req), entriesPtr_(entries), entriesCount_(N) {}

    /// @brief コンストラクタ互換（エントリ無し）
    constexpr explicit JsonEnumField(MemberPtrType memberPtr, const char* keyName, bool req = false)
        : Base(memberPtr, keyName, req) {}

    /// @brief Enum値を文字列に変換する。
    /// @param value 変換対象のenum値。
    /// @return JSON文字列。見つからない場合は例外を投げる。
    void toJson(JsonWriter& writer, const ValueType& value) const {
        for (std::size_t i = 0; i < entriesCount_; ++i) {
            if (entriesPtr_[i].value == value) {
                writer.writeObject(entriesPtr_[i].name);
                return;
            }
        }
        throw std::runtime_error("Failed to convert enum to string");
    }

    /// @brief JsonParser から Enum値に変換する。
    /// @param parser JsonParser インスタンス（現在の値を読み取るために使用される）。
    /// @return 変換されたenum値。見つからない場合は例外を投げる。
    /// @note 内部で文字列を読み取り、enumエントリで検索する。
    ValueType fromJson(JsonParser& parser) const {
        std::string jsonValue;
        parser.readTo(jsonValue);

        for (std::size_t i = 0; i < entriesCount_; ++i) {
            if (jsonValue == entriesPtr_[i].name) {
                return entriesPtr_[i].value;
            }
        }
        throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
    }
};

/// @brief 型名とファクトリ関数のマッピングエントリ。
/// @tparam BaseType 基底クラス型。
export template <typename BaseType>
struct PolymorphicTypeEntry {
    const char* typeName; ///< JSON上の型名。
    std::unique_ptr<BaseType> (*factory)(); ///< オブジェクト生成関数ポインタ。
};

// 共通: polymorphic オブジェクト一つ分の読み取りを行うヘルパー
// - parser の現在位置は null または startObject のいずれかであることを期待
// - 成功した場合、std::unique_ptr<BaseType>（null を示す場合は nullptr）を返す
template <typename BaseType>
std::unique_ptr<BaseType> readPolymorphicInstance(JsonParser& parser, const PolymorphicTypeEntry<BaseType>* entriesPtr, std::size_t entriesCount) {
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }

    parser.startObject();

    std::string typeKey = parser.nextKey();
    if (typeKey != "type") {
        throw std::runtime_error(std::string("Expected 'type' key for polymorphic object, got '") + typeKey + "'");
    }

    std::string typeName;
    parser.readTo(typeName);

    const PolymorphicTypeEntry<BaseType>* entry = nullptr;
    for (std::size_t i = 0; i < entriesCount; ++i) {
        if (entriesPtr[i].typeName == typeName) { entry = &entriesPtr[i]; break; }
    }
    if (!entry) {
        throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeName);
    }

    auto tmp = entry->factory();

    if constexpr (HasJsonFields<BaseType>) {
        auto& fields = tmp->jsonFields();
        while (!parser.nextIsEndObject()) {
            std::string k = parser.nextKey();
            if (!fields.readFieldByKey(parser, tmp.get(), k)) {
                parser.noteUnknownKey(k);
                parser.skipValue();
            }
        }
    } else {
        // 型情報がない場合は残りをスキップ
        while (!parser.nextIsEndObject()) {
            std::string k = parser.nextKey();
            parser.noteUnknownKey(k);
            parser.skipValue();
        }
    }

    parser.endObject();
    return tmp;
}

/// @brief ポリモーフィック型（unique_ptr<基底クラス>）用のJsonField派生クラス。
/// @tparam MemberPtr unique_ptr<基底クラス>メンバー変数へのポインタ。
/// @tparam Entries 型名とファクトリ関数のマッピング配列への参照。
export template <typename MemberPtrType>
struct JsonPolymorphicField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;

    // ValueTypeはstd::unique_ptr<T>であることを確認
    static_assert(std::is_same_v<ValueType, std::unique_ptr<typename ValueType::element_type>>,
                  "JsonPolymorphicField requires std::unique_ptr type");

    using BaseType = typename ValueType::element_type;

    // 実行時に渡されるエントリ配列への参照とサイズ
    const PolymorphicTypeEntry<BaseType>* entriesPtr_{nullptr};
    std::size_t entriesCount_{0};

    /// @brief ポリモーフィック型用フィールドのコンストラクタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    // コンストラクタ: 書式は (memberPtr, keyName, entriesArray, req=false)
    template <std::size_t N>
    constexpr explicit JsonPolymorphicField(MemberPtrType memberPtr, const char* keyName, const PolymorphicTypeEntry<BaseType> (&entries)[N], bool req = false)
        : Base(memberPtr, keyName, req), entriesPtr_(entries), entriesCount_(N) {}

    /// @brief 型名から対応するエントリを検索する。
    /// @param typeName 検索する型名。
    /// @return 見つかった場合はエントリへのポインタ、見つからない場合はnullptr。
    const PolymorphicTypeEntry<BaseType>* findEntry(std::string_view typeName) const {
        for (std::size_t i = 0; i < entriesCount_; ++i) {
            if (entriesPtr_[i].typeName == typeName) {
                return &entriesPtr_[i];
            }
        }
        return nullptr;
    }

    /// @brief オブジェクトから型名を取得する。
    /// @param obj 対象オブジェクト。
    /// @return 型名。見つからない場合は例外を投げる。
    std::string getTypeName(const BaseType& obj) const {
        for (std::size_t i = 0; i < entriesCount_; ++i) {
            auto testObj = entriesPtr_[i].factory();
            if (typeid(obj) == typeid(*testObj)) {
                return entriesPtr_[i].typeName;
            }
        }
        throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
    }

    /// @brief JsonParser から polymorphic オブジェクト（unique_ptr<T>）を読み込む。
    /// @param parser JsonParser の参照。現在の位置にオブジェクトか null があることを期待する。
    /// @return 要素型 T を保持する unique_ptr。null の場合は nullptr を返す。
    ValueType fromJson(JsonParser& parser) const {
        return readPolymorphicInstance<BaseType>(parser, entriesPtr_, entriesCount_);
    }

    /// @brief JsonWriter に対して polymorphic オブジェクトを書き出す。
    /// @param writer JsonWriter の参照。
    /// @param ptr 書き込み対象の unique_ptr（null 可）。
    void toJson(JsonWriter& writer, const ValueType& ptr) const {
        if (!ptr) {
            writer.null();
            return;
        }
        writer.startObject();
        std::string typeName = getTypeName(*ptr);
        writer.key("type");
        writer.writeObject(typeName);
        auto& fields = ptr->jsonFields();
        fields.writeFieldsOnly(writer, ptr.get());
        writer.endObject();
    }
};

/// @brief ポリモーフィックな配列（vector<std::unique_ptr<BaseType>>）用のフィールド。
export template <typename MemberPtrType>
struct JsonPolymorphicArrayField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;

    // ValueType は std::vector<std::unique_ptr<T>> であることを確認するユーティリティ
    template <typename X>
    struct is_vector_of_unique_ptr : std::false_type {};

    template <typename U>
    struct is_vector_of_unique_ptr<std::vector<std::unique_ptr<U>>> : std::true_type {};

    static_assert(is_vector_of_unique_ptr<ValueType>::value, "JsonPolymorphicArrayField requires std::vector<std::unique_ptr<T>> type");

    using ElementUniquePtr = typename ValueType::value_type; // std::unique_ptr<T>
    using BaseType = typename ElementUniquePtr::element_type;

    const PolymorphicTypeEntry<BaseType>* entriesPtr_{nullptr};
    std::size_t entriesCount_{0};

    template <std::size_t N>
    constexpr explicit JsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName, const PolymorphicTypeEntry<BaseType> (&entries)[N], bool req = false)
        : Base(memberPtr, keyName, req), entriesPtr_(entries), entriesCount_(N) {}

    const PolymorphicTypeEntry<BaseType>* findEntry(std::string_view typeName) const {
        for (std::size_t i = 0; i < entriesCount_; ++i) {
            if (entriesPtr_[i].typeName == typeName) return &entriesPtr_[i];
        }
        return nullptr;
    }

    std::string getTypeName(const BaseType& obj) const {
        for (std::size_t i = 0; i < entriesCount_; ++i) {
            auto testObj = entriesPtr_[i].factory();
            if (typeid(obj) == typeid(*testObj)) return entriesPtr_[i].typeName;
        }
        throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
    }

    /// @brief JsonParser から polymorphic 配列（vector<std::unique_ptr<T>>）を読み込む。
    /// @param parser JsonParser の参照。現在の位置に配列があることを期待する。
    /// @return 読み込まれたベクター（要素は unique_ptr<T>）。
    ValueType fromJson(JsonParser& parser) const {
        ValueType out;
        parser.startArray();
        out.clear();
        while (!parser.nextIsEndArray()) {
            out.push_back(readPolymorphicInstance<BaseType>(parser, entriesPtr_, entriesCount_));
        }
        parser.endArray();
        return out;
    }

    /// @brief JsonWriter に対して polymorphic 配列を書き出す。
    /// @param writer JsonWriter の参照。
    /// @param vec 書き込み対象の vector<std::unique_ptr<T>>。
    void toJson(JsonWriter& writer, const ValueType& vec) const {
        writer.startArray();
        for (const auto& ptr : vec) {
            if (!ptr) {
                writer.null();
                continue;
            }
            writer.startObject();
            std::string typeName = getTypeName(*ptr);
            writer.key("type");
            writer.writeObject(typeName);
            auto& fields = ptr->jsonFields();
            fields.writeFieldsOnly(writer, ptr.get());
            writer.endObject();
        }
        writer.endArray();
    }
};

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
        : fieldMap_(fields...), fields_(std::move(fields)...) {
        validateFields(); // ここで検証を実行
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

            // toJsonメンバーを持つ場合はそれを使用
            if constexpr (HasToJson<FieldType, std::remove_cvref_t<decltype(value)>>) {
                field.toJson(writer, value);
            } else {
                // ここでエラーになる場合は、fieldの対象メンバー変数の型が、json入出力対象対象型ではない。
                writeObject(writer, value);
            }
        });
    }
private:
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

    // ポリモーフィック型の書き出しは各 JsonPolymorphicField/JsonPolymorphicArrayField の
    // toJson(JsonWriter&, value) に移譲されています。

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
    void readObject(JsonParser& parser, T& out) const {
        auto& fields = out.jsonFields();
        parser.startObject();
        readObjectFieldsByFieldSet(parser, fields, &out);
        parser.endObject();
    }

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
                using FieldType = std::remove_cvref_t<decltype(field)>;
                using ValueType = typename FieldType::ValueType;

                // fromJson メンバーを持つ場合はそれを使用（ポリモーフィック含む）
                if constexpr (HasFromJson<FieldType, ValueType>) {
                    obj.*(field.member) = field.fromJson(parser);
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
    void readObject(JsonParser& parser, T& out) const {
        T temp = out;
        parser.readTo(temp);
        out = temp;
    }

    void readObject(JsonParser& parser, std::string& out) const {
        parser.readTo(out);
    }

    /// @brief JsonFieldSetBaseを使用してオブジェクトのフィールドを読み込む（startObject/endObject済み）。
    /// @tparam T オブジェクトの型。
    /// @param parser 読み取り元のJsonParser互換オブジェクト。
    /// @param fields オブジェクトのフィールドセット。
    /// @param obj 読み込み先のオブジェクト。
    /// @note この関数は、オブジェクトが既にstartObjectされている状態を想定している。
    template <typename T>
    void readObjectFieldsByFieldSet(JsonParser& parser, const JsonFieldSetBase& fields, T* obj) const {
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
    void readObject(JsonParser& parser, std::unique_ptr<T>& out) const {
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
    template <typename T>
        requires HasReadJson<T>
    void readObject(JsonParser& parser, T& out) const {
        out.readJson(parser);
    }

    // 配列
    template <typename T>
    void readObject(JsonParser& parser, std::vector<T>& out) const {
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

            // fromJson メンバーを持つ場合はそれを使用（ポリモーフィック含む）
            if constexpr (HasFromJson<FieldType, ValueType>) {
                owner->*(field.member) = field.fromJson(parser);
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

    // Use std::string_view for keys so lookups using std::string_view work
    // reliably without the SortedHashArrayMap needing string_view-specific code.
    SortedHashArrayMap<std::string_view, bool, N_> fieldMap_{}; ///< ハッシュ順に整列したフィールド情報。
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
