// @file JsonField.cppm
// @brief JSONフィールドの定義。構造体とJSONの相互変換を提供する。

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
#include <vector>

export module rai.json.json_field;

import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
import rai.collection.sorted_hash_array_map;

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

/// @brief ポインタ型から要素型を抽出するメタ関数。
/// @tparam T ポインタ型（unique_ptr<T>、shared_ptr<T>、T*）。
template <typename T>
struct PointerElementType;

template <typename T>
struct PointerElementType<std::unique_ptr<T>> {
    using type = T;
};

template <typename T>
struct PointerElementType<std::shared_ptr<T>> {
    using type = T;
};

template <typename T>
struct PointerElementType<T*> {
    using type = T;
};

/// @brief ポインタ型（unique_ptr/shared_ptr/生ポインタ）であることを確認するconcept。
template <typename T>
concept SmartOrRawPointer = requires {
    typename PointerElementType<T>::type;
} && (std::is_same_v<T, std::unique_ptr<typename PointerElementType<T>::type>> ||
      std::is_same_v<T, std::shared_ptr<typename PointerElementType<T>::type>> ||
      std::is_same_v<T, typename PointerElementType<T>::type*>);

/// @brief ポインタ型のvectorであることを確認するconcept。
template <typename T>
concept VectorOfPointers = requires {
    typename T::value_type;
} && SmartOrRawPointer<typename T::value_type> &&
     (std::is_same_v<T, std::vector<typename T::value_type>>);

// null は全ポインタ型（unique_ptr/shared_ptr/生ポインタ）へ暗黙変換可能のため、
// 専用ヘルパーは不要（nullptr を直接返す）。

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

/// @brief ポリモーフィック型用のファクトリ関数型（ポインタ型を返す）。
export template <typename PtrType>
    requires SmartOrRawPointer<PtrType>
using PolymorphicTypeFactory = std::function<PtrType()>;

/// @brief 共通: polymorphic オブジェクト一つ分の読み取りを行うヘルパー（N非依存版）
template <typename PtrType>
    requires SmartOrRawPointer<PtrType>
PtrType readPolymorphicInstance(JsonParser& parser,
    const collection::MapReference<std::string_view, PolymorphicTypeFactory<PtrType>>& entriesMap,
    std::string_view jsonKey = "type") {
    using BaseType = typename PointerElementType<PtrType>::type;
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
        BaseType* raw = std::to_address(tmp);
        while (!parser.nextIsEndObject()) {
            std::string k = parser.nextKey();
            if (!fields.readFieldByKey(parser, raw, k)) {
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
export template <typename MemberPtrType>
    requires SmartOrRawPointer<typename JsonField<MemberPtrType>::ValueType>
struct JsonPolymorphicField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;
    using BaseType = typename PointerElementType<ValueType>::type;
    using Key = std::string_view;
    using Value = PolymorphicTypeFactory<ValueType>;
    using Map = collection::MapReference<Key, Value>;

    /// @brief ポリモーフィック型用フィールドのコンストラクタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    // コンストラクタ: 書式は (memberPtr, keyName, entriesArray, req=false)
    constexpr explicit JsonPolymorphicField(MemberPtrType memberPtr, const char* keyName,
        Map entries, const char* jsonKey = "type", bool req = false)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    /// @brief ポリモーフィック型用フィールドのコンストラクタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    // コンストラクタ: 書式は (memberPtr, keyName, entriesArray, req=false)
    template <size_t N, typename Traits>
    constexpr explicit JsonPolymorphicField(MemberPtrType memberPtr, const char* keyName,
        const collection::SortedHashArrayMap<Key, Value, N, Traits>& entries, const char* jsonKey = "type", bool req = false)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    /// @brief 型名から対応するエントリを検索する。
    /// @param typeName 検索する型名。
    /// @return 見つかった場合はエントリへのポインタ、見つからない場合はnullptr。
    const PolymorphicTypeFactory<ValueType>* findEntry(std::string_view typeName) const {
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
        if (!found) {
            throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
        }
        return result;
    }

    /// @brief JsonParser から polymorphic オブジェクトを読み込む。
    /// @param parser JsonParser の参照。現在の位置にオブジェクトか null があることを期待する。
    /// @return 要素型 T を保持するポインタ（unique_ptr/shared_ptr/生ポインタ）。null の場合は nullptr を返す。
    ValueType fromJson(JsonParser& parser) const {
        return readPolymorphicInstance<ValueType>(parser, nameToEntry_, jsonKey_);
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
        fields.writeFieldsOnly(writer, std::to_address(ptr));
        writer.endObject();
    }

private:
    //! entries は typeName -> entry のマップ参照として保持
    Map nameToEntry_;

    //! JSON 内で型を判別するためのキー名（デフォルトは "type"）
    const char* jsonKey_;
};

/// @brief ポリモーフィックな配列（vector<std::unique_ptr<BaseType>>）用のフィールド。
export template <typename MemberPtrType>
    requires VectorOfPointers<typename JsonField<MemberPtrType>::ValueType>
struct JsonPolymorphicArrayField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;
    using ElementPtrType = typename ValueType::value_type; // std::unique_ptr<T>, std::shared_ptr<T>, or T*
    using BaseType = typename PointerElementType<ElementPtrType>::type;
    using Key = std::string_view;
    using Value = PolymorphicTypeFactory<ElementPtrType>;
    using Map = collection::MapReference<Key, Value>;

    constexpr explicit JsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName,
        Map entries, const char* jsonKey = "type", bool req = false)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    template <size_t N, typename Traits>
    constexpr explicit JsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName,
        const collection::SortedHashArrayMap<Key, Value, N, Traits>& entries, const char* jsonKey = "type", bool req = false)
        : Base(memberPtr, keyName, req), nameToEntry_(Map(entries)), jsonKey_(jsonKey) {}

    const PolymorphicTypeFactory<ElementPtrType>* findEntry(std::string_view typeName) const {
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

    /// @brief JsonParser から polymorphic 配列を読み込む。
    /// @param parser JsonParser の参照。現在の位置に配列があることを期待する。
    /// @return 読み込まれたベクター（要素は unique_ptr<T>/shared_ptr<T>/T*）。
    ValueType fromJson(JsonParser& parser) const {
        ValueType out;
        parser.startArray();
        out.clear();
        while (!parser.nextIsEndArray()) {
            auto elem = readPolymorphicInstance<ElementPtrType>(parser, nameToEntry_, jsonKey_);
            out.push_back(std::move(elem));
        }
        parser.endArray();
        return out;
    }

    /// @brief JsonWriter に対して polymorphic 配列を書き出す。
    /// @param writer JsonWriter の参照。
    /// @param vec 書き込み対象の vector（要素は unique_ptr<T>/shared_ptr<T>/T*）。
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
            fields.writeFieldsOnly(writer, std::to_address(ptr));
            writer.endObject();
        }
        writer.endArray();
    }

private:
    Map nameToEntry_;

    //! JSON 内で型を判別するためのキー名
    const char* jsonKey_;
};

}  // namespace rai::json
