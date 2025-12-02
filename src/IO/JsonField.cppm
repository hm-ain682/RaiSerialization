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

}  // namespace rai::json
