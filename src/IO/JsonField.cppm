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

import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
import rai.collection.sorted_hash_array_map;
export module rai.json.json_field;

namespace rai::json {

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

// ******************************************************************************** フィールド定義

/// @brief JSONフィールドの基本定義。
/// @tparam MemberPtr メンバー変数へのポインタ。
export template <typename MemberPtrType>
struct JsonField {
    static_assert(std::is_member_object_pointer_v<MemberPtrType>,
        "JsonField requires a data member pointer");
    using Traits = MemberPointerTraits<MemberPtrType>;
    using OwnerType = typename Traits::OwnerType;
    using ValueType = typename Traits::ValueType;

    constexpr explicit JsonField(MemberPtrType memberPtr, const char* keyName, bool req = false)
        : member(memberPtr), key(keyName), required(req) {}

    MemberPtrType member{}; // pointer-to-member stored at runtime
    const char* key{};
    bool required{false};
};

/// @brief Enumと文字列のマッピングエントリ。
/// @tparam EnumType 対象のenum型。
export template <typename EnumType>
struct EnumEntry {
    EnumType value;      ///< Enum値。
    const char* name;    ///< 対応する文字列名。
};

/// @brief Enum型のフィールド用に特化したJsonField派生クラス。
/// @tparam MemberPtr Enumメンバー変数へのポインタ。
/// @tparam Entries Enumと文字列のマッピング配列への参照。
export template <typename MemberPtrType, std::size_t N = 0>
struct JsonEnumField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;
    static_assert(std::is_enum_v<ValueType>, "JsonEnumField requires enum type");

    /// @brief Enum用フィールドのコンストラクタ（エントリ配列を受け取る）
    /// @param memberPtr ポインタメンバ
    /// @param keyName JSONキー名
    /// @param entries エントリ配列
    constexpr explicit JsonEnumField(MemberPtrType memberPtr, const char* keyName,
        const EnumEntry<ValueType> (&entries)[N], bool req = false)
        : Base(memberPtr, keyName, req) {

        // build name -> value descriptor array
        std::pair<std::string_view, ValueType> nv[N];
        for (std::size_t i = 0; i < N; ++i) {
            nv[i] = { entries[i].name, entries[i].value };
        }
        nameToValue_ = collection::SortedHashArrayMap<std::string_view, ValueType, N>(nv);

        // build value -> name descriptor array
        std::pair<ValueType, std::string_view> vn[N];
        for (std::size_t i = 0; i < N; ++i) {
            vn[i] = { entries[i].value, entries[i].name };
        }
        valueToName_ = collection::SortedHashArrayMap<ValueType, std::string_view, N>(vn);
    }

    /// @brief Enum値を文字列に変換する。
    /// @param value 変換対象のenum値。
    /// @return JSON文字列。見つからない場合は例外を投げる。
    void toJson(JsonWriter& writer, const ValueType& value) const {
        if constexpr (N == 0) {
            throw std::runtime_error("Failed to convert enum to string");
        }
        const auto* opt = valueToName_.findValue(value);
        if (opt) {
            writer.writeObject(*opt);
            return;
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

        if constexpr (N == 0) {
            throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
        }
        const auto* found = nameToValue_.findValue(jsonValue);
        if (found) return *found;
        throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
    }

private:
    // エントリは SortedHashArrayMap に保持する（キー->値 / 値->キー で2方向検索を高速化）
    collection::SortedHashArrayMap<std::string_view, ValueType, N> nameToValue_{}; ///< name -> enum value
    collection::SortedHashArrayMap<ValueType, std::string_view, N> valueToName_{}; ///< enum value -> name
};

/// @brief ポリモーフィック型用のファクトリ関数型。
export template <typename BaseType>
using PolymorphicTypeFactory = std::function<std::unique_ptr<BaseType>()>;

/// @brief 共通: polymorphic オブジェクト一つ分の読み取りを行うヘルパー
/// - parser の現在位置は null または startObject のいずれかであることを期待
/// - 成功した場合、std::unique_ptr<BaseType>（null を示す場合は nullptr）を返す
template <typename BaseType, std::size_t N>
std::unique_ptr<BaseType> readPolymorphicInstance(JsonParser& parser,
    const collection::SortedHashArrayMap<std::string_view, PolymorphicTypeFactory<BaseType>, N>& entriesMap,
    std::string_view jsonKey = "type") {
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }

    parser.startObject();

    std::string typeKey = parser.nextKey();
    if (typeKey != jsonKey) {
        throw std::runtime_error(std::string("Expected '") + std::string(jsonKey) + "' key for polymorphic object, got '" + typeKey + "'");
    }

    std::string typeName;
    parser.readTo(typeName);

    const auto* entryPtr = entriesMap.findValue(typeName);
    if (!entryPtr) {
        throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeName);
    }

    auto tmp = (*entryPtr)();

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
export template <typename MemberPtrType, std::size_t N = 0>
struct JsonPolymorphicField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;
    using BaseType = typename ValueType::element_type;
    using Map = collection::SortedHashArrayMap<std::string_view, PolymorphicTypeFactory<BaseType>, N>;

    // ValueTypeはstd::unique_ptr<T>であることを確認
    static_assert(std::is_same_v<ValueType, std::unique_ptr<typename ValueType::element_type>>,
                  "JsonPolymorphicField requires std::unique_ptr type");

    /// @brief ポリモーフィック型用フィールドのコンストラクタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    // コンストラクタ: 書式は (memberPtr, keyName, entriesArray, req=false)
    constexpr explicit JsonPolymorphicField(MemberPtrType memberPtr, const char* keyName,
        const Map& entries, const char* jsonKey = "type", bool req = false)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    /// @brief 型名から対応するエントリを検索する。
    /// @param typeName 検索する型名。
    /// @return 見つかった場合はエントリへのポインタ、見つからない場合はnullptr。
    const PolymorphicTypeFactory<BaseType>* findEntry(std::string_view typeName) const {
        return nameToEntry_.findValue(typeName);
    }

    /// @brief オブジェクトから型名を取得する。
    /// @param obj 対象オブジェクト。
    /// @return 型名。見つからない場合は例外を投げる。
    std::string getTypeName(const BaseType& obj) const {
        bool found = false;
        std::string result;
        for (auto& it : nameToEntry_) {
            auto testObj = it.value();
            if (!found && typeid(obj) == typeid(*testObj)) {
                found = true;
                result = it.key;
            }
        }
        if (!found) throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
        return result;
    }

    /// @brief JsonParser から polymorphic オブジェクト（unique_ptr<T>）を読み込む。
    /// @param parser JsonParser の参照。現在の位置にオブジェクトか null があることを期待する。
    /// @return 要素型 T を保持する unique_ptr。null の場合は nullptr を返す。
    ValueType fromJson(JsonParser& parser) const {
        return readPolymorphicInstance<BaseType, N>(parser, nameToEntry_, jsonKey_);
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
        writer.key(jsonKey_);
        writer.writeObject(typeName);
        auto& fields = ptr->jsonFields();
        fields.writeFieldsOnly(writer, ptr.get());
        writer.endObject();
    }

private:
    //! entries は typeName -> entry のマップ参照として保持
    const Map& nameToEntry_;

    //! JSON 内で型を判別するためのキー名（デフォルトは "type"）
    const char* jsonKey_;
};

/// @brief ポリモーフィックな配列（vector<std::unique_ptr<BaseType>>）用のフィールド。
export template <typename MemberPtrType, std::size_t N = 0>
struct JsonPolymorphicArrayField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;
    using ElementUniquePtr = typename ValueType::value_type; // std::unique_ptr<T>
    using BaseType = typename ElementUniquePtr::element_type;
    using Map = collection::SortedHashArrayMap<std::string_view, PolymorphicTypeFactory<BaseType>, N>;

    // ValueType は std::vector<std::unique_ptr<T>> であることを確認する
    template <typename X>
    struct is_vector_of_unique_ptr : std::false_type {};
    template <typename U>
    struct is_vector_of_unique_ptr<std::vector<std::unique_ptr<U>>> : std::true_type {};
    static_assert(is_vector_of_unique_ptr<ValueType>::value,
        "JsonPolymorphicArrayField requires std::vector<std::unique_ptr<T>> type");

    constexpr explicit JsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName,
        const Map& entries, const char* jsonKey = "type", bool req = false)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    const PolymorphicTypeFactory<BaseType>* findEntry(std::string_view typeName) const {
        return nameToEntry_.findValue(typeName);
    }

    std::string getTypeName(const BaseType& obj) const {
        bool found = false;
        std::string result;
        for (auto& it : nameToEntry_) {
            if (!found) {
                auto testObj = it.value();
                if (typeid(obj) == typeid(*testObj)) {
                    found = true;
                    result = it.key;
                }
            }
        }
        if (found) return result;
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
            out.push_back(readPolymorphicInstance<BaseType, N>(parser, nameToEntry_, jsonKey_));
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
            writer.key(jsonKey_);
            writer.writeObject(typeName);
            auto& fields = ptr->jsonFields();
            fields.writeFieldsOnly(writer, ptr.get());
            writer.endObject();
        }
        writer.endArray();
    }

private:
    const Map& nameToEntry_;

    //! JSON 内で型を判別するためのキー名
    const char* jsonKey_;
};

}  // namespace rai::json
