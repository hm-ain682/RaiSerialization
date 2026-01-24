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
#include <set>
#include <unordered_set>

export module rai.json.json_polymorphic;

import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
import rai.json.json_field;
import rai.json.json_value_io;
import rai.collection.sorted_hash_array_map;

namespace rai::json {

// ------------------------- Polymorphic helpers and fields -------------------------

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

/// @brief ポリモーフィック型用のファクトリ関数型（ポインタ型を返す）。
export template <typename PtrType>
    requires IsSmartOrRawPointer<PtrType>
using PolymorphicTypeFactory = std::function<PtrType()>;

/// @brief ポリモーフィックオブジェクト1つ分を読み取るヘルパー関数。
/// @tparam PtrType ポインタ型（unique_ptr/shared_ptr/生ポインタ）。
/// @param parser JsonParserの参照。
/// @param entriesMap 型名からファクトリ関数へのマッピング。
/// @param jsonKey 型判別用のJSONキー名。
/// @return 読み取ったオブジェクトのポインタ。または型キーが見つからない／未知の型名の場合はnullptr。
export template <typename PtrType>
    requires IsSmartOrRawPointer<PtrType>
PtrType readPolymorphicInstance(
    JsonParser& parser,
    const collection::MapReference<std::string_view, PolymorphicTypeFactory<PtrType>>& entriesMap,
    std::string_view jsonKey = "type") {

    parser.startObject();

    // 最初のキーが型判別キーであることを確認
    std::string typeKey = parser.nextKey();
    if (typeKey != jsonKey) {
        throw std::runtime_error(
            std::string("Expected '") + std::string(jsonKey) +
            "' key for polymorphic object, got '" + typeKey + "'");
    }

    // 型名を読み取り、対応するファクトリを検索
    std::string typeName;
    parser.readTo(typeName);
    const auto* factory = entriesMap.findValue(typeName);
    if (!factory) {
        return nullptr;
    }

    // ファクトリでインスタンスを生成
    auto instance = (*factory)();
    using BaseType = typename PointerElementType<PtrType>::type;

    // HasJsonFieldsを持つ型の場合、残りのフィールドを読み取る
    if constexpr (HasJsonFields<BaseType>) {
        auto& fields = instance->jsonFields();
        BaseType* raw = std::to_address(instance);
        while (!parser.nextIsEndObject()) {
            std::string key = parser.nextKey();
            if (!fields.readFieldByKey(parser, raw, key)) {
                // 未知のキーはスキップ
                parser.noteUnknownKey(key);
                parser.skipValue();
            }
        }
    }
    else {
        // jsonFieldsを持たない型の場合、全フィールドをスキップ
        while (!parser.nextIsEndObject()) {
            std::string key = parser.nextKey();
            parser.noteUnknownKey(key);
            parser.skipValue();
        }
    }

    parser.endObject();
    return instance;
}

/// @brief ポリモーフィックオブジェクト1つ分を読み取るヘルパー関数（null許容版）。
export template <typename PtrType>
    requires IsSmartOrRawPointer<PtrType>
PtrType readPolymorphicInstanceOrNull(
    JsonParser& parser,
    const collection::MapReference<std::string_view, PolymorphicTypeFactory<PtrType>>& entriesMap,
    std::string_view jsonKey = "type") {
    // null値の場合はnullptrを返す
    if (parser.nextIsNull()) {
        parser.skipValue();
        return nullptr;
    }
    // オブジェクトの場合は通常の読み取り処理
    auto position = parser.nextPosition();
    auto instance = readPolymorphicInstance<PtrType>(parser, entriesMap, jsonKey);
    if (instance == nullptr) {
         throw std::runtime_error("Unknown polymorphic type: " + std::to_string(position));
    }
    return instance;
}

/// @brief ポリモーフィック型（unique_ptr<基底クラス>）用のJsonField派生クラス。
/// @tparam MemberPtr unique_ptr<基底クラス>メンバー変数へのポインタ。
/// @tparam Entries 型名とファクトリ関数のマッピング配列への参照。
export template <typename MemberPtrType>
    requires IsSmartOrRawPointer<MemberPointerValueType<MemberPtrType>>
struct JsonPolymorphicField : JsonFieldBase<MemberPtrType> {
    using ValueType = MemberPointerValueType<MemberPtrType>; 
    using Base = JsonFieldBase<MemberPtrType>;
    using BaseType = typename PointerElementType<ValueType>::type;
    using Key = std::string_view;
    using Value = PolymorphicTypeFactory<ValueType>;
    using Map = collection::MapReference<Key, Value>;

    /// @brief ポリモーフィック型用フィールドのコンストラクタ。
    /// @param memberPtr メンバー変数へのポインタ。
    /// @param keyName JSONキー名。
    /// @param entries 型名からファクトリ関数へのマッピング。
    /// @param jsonKey JSON内で型を判別するためのキー名。
    /// @param req 必須フィールドかどうか。
    constexpr explicit JsonPolymorphicField(MemberPtrType memberPtr, const char* keyName,
        Map entries, const char* jsonKey = "type", bool req = true)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    /// @brief ポリモーフィック型用フィールドのコンストラクタ（SortedHashArrayMap版）。
    /// @param memberPtr メンバー変数へのポインタ。
    /// @param keyName JSONキー名。
    /// @param entries 型名からファクトリ関数へのマッピング（SortedHashArrayMap）。
    /// @param jsonKey JSON内で型を判別するためのキー名。
    /// @param req 必須フィールドかどうか。
    template <size_t N, typename Traits>
    constexpr explicit JsonPolymorphicField(MemberPtrType memberPtr, const char* keyName,
        const collection::SortedHashArrayMap<Key, Value, N, Traits>& entries,
        const char* jsonKey = "type", bool req = true)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    /// @brief 型名から対応するエントリを検索する。
    /// @param typeName 検索する型名。
    /// @return 見つかった場合はエントリへのポインタ、見つからない場合はnullptr。
    const PolymorphicTypeFactory<ValueType>* findEntry(std::string_view typeName) const {
        return nameToEntry_.findValue(typeName);
    }

    /// @brief オブジェクトから型名を取得する。
    /// @param obj 対象オブジェクト。
    /// @return 型名。
    /// @throws std::runtime_error マッピングに存在しない型の場合。
    std::string getTypeName(const BaseType& obj) const {
        // 全エントリを走査し、typeidで一致する型名を検索
        for (const auto& it : nameToEntry_) {
            auto testObj = it.value();
            if (typeid(obj) == typeid(*testObj)) {
                return std::string(it.key);
            }
        }
        // マッピングに存在しない型の場合
        throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
    }

    /// @brief JsonParser から polymorphic オブジェクトを読み込む。
    /// @param parser JsonParser の参照。現在の位置にオブジェクトか null があることを期待する。
    /// @return 要素型 T を保持するポインタ（unique_ptr/shared_ptr/生ポインタ）。null の場合は nullptr を返す。
    ValueType fromJson(JsonParser& parser) const {
        return readPolymorphicInstanceOrNull<ValueType>(parser, nameToEntry_, jsonKey_);
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
    Map nameToEntry_;     ///< 型名からファクトリ関数へのマッピング。
    const char* jsonKey_; ///< JSON内で型を判別するキー名。
};

/// @brief ポリモーフィックな配列（vector<std::unique_ptr<BaseType>>）用のフィールド。
export template <typename MemberPtrType>
    requires IsVectorOfPointers<MemberPointerValueType<MemberPtrType>>
struct JsonPolymorphicArrayField : JsonFieldBase<MemberPtrType> {
    using ValueType = MemberPointerValueType<MemberPtrType>; 
    using Base = JsonFieldBase<MemberPtrType>;
    using ElementPtrType = typename ValueType::value_type; ///< ポインタ要素型。
    using BaseType = typename PointerElementType<ElementPtrType>::type;
    using Key = std::string_view;
    using Value = PolymorphicTypeFactory<ElementPtrType>;
    using Map = collection::MapReference<Key, Value>;

    /// @brief コンストラクタ。
    /// @param memberPtr メンバー変数へのポインタ。
    /// @param keyName JSONキー名。
    /// @param entries 型名からファクトリ関数へのマッピング。
    /// @param jsonKey JSON内で型を判別するキー名。
    /// @param req 必須フィールドかどうか。
    explicit JsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName,
        Map entries, const char* jsonKey = "type", bool req = true)
        : Base(memberPtr, keyName, req), nameToEntry_(entries), jsonKey_(jsonKey) {}

    /// @brief コンストラクタ（SortedHashArrayMap版）。
    /// @param memberPtr メンバーポインタ。
    /// @param keyName JSONキー名。
    /// @param entries 型名とファクトリ関数のマッピング。
    /// @param jsonKey JSON内で型を判別するキー名。
    /// @param req 必須フィールドかどうか。
    template <size_t N, typename Traits>
    explicit JsonPolymorphicArrayField(MemberPtrType memberPtr, const char* keyName,
        const collection::SortedHashArrayMap<Key, Value, N, Traits>& entries,
        const char* jsonKey = "type", bool req = true)
        : JsonPolymorphicArrayField(memberPtr, keyName, Map(entries), jsonKey, req) {}

    /// @brief 型名から対応するファクトリ関数を検索する。
    /// @param typeName 検索する型名。
    /// @return 見つかった場合はファクトリ関数へのポインタ。
    const PolymorphicTypeFactory<ElementPtrType>* findEntry(std::string_view typeName) const {
        return nameToEntry_.findValue(typeName);
    }

    /// @brief JsonParserからポリモーフィック配列を読み取る。
    /// @param parser JsonParserの参照。
    /// @return 読み取った配列。
    ValueType fromJson(JsonParser& parser) const {
        ValueType out{};
        parser.startArray();
        while (!parser.nextIsEndArray()) {
            auto elem = readPolymorphicInstanceOrNull<ElementPtrType>(parser, nameToEntry_, jsonKey_);
            out.push_back(std::move(elem));
        }
        parser.endArray();
        return out;
    }

    /// @brief JsonWriterにポリモーフィック配列を書き出す。
    /// @param writer JsonWriterの参照。
    /// @param container 書き出すコンテナ。
    void toJson(JsonWriter& writer, const ValueType& container) const {
        writer.startArray();
        for (const auto& elem : container) {
            writeElement(writer, elem, nameToEntry_, jsonKey_);
        }
        writer.endArray();
    }

    /// @brief オブジェクトから型名を取得する。
    /// @param obj 対象オブジェクト。
    /// @return 型名。
    std::string getTypeName(const BaseType& obj) const {
        return getTypeNameFromMap(obj, nameToEntry_);
    }

private:
    Map nameToEntry_; ///< 型名からファクトリ関数へのマッピング。
    const char* jsonKey_; ///< JSON内で型を判別するキー名。

    /// @brief オブジェクトから型名を検索する。
    /// @param obj 対象オブジェクト。
    /// @param entries 型名とファクトリ関数のマッピング。
    /// @return 型名。
    static std::string getTypeNameFromMap(const BaseType& obj, Map entries) {
        for (const auto& it : entries) {
            auto testObj = it.value();
            if (typeid(obj) == typeid(*testObj)) {
                return std::string(it.key);
            }
        }
        throw std::runtime_error(std::string("Unknown polymorphic type: ") + typeid(obj).name());
    }

    /// @brief ポリモーフィック要素を書き出す。
    /// @param writer JsonWriterの参照。
    /// @param ptr 書き出す要素。
    /// @param entries 型名とファクトリ関数のマッピング。
    /// @param jsonKey JSON内で型を判別するキー名。
    static void writeElement(JsonWriter& writer, const ElementPtrType& ptr,
        Map entries, const char* jsonKey) {
        if (!ptr) {
            writer.null();
            return;
        }
        writer.startObject();
        std::string typeName = getTypeNameFromMap(*ptr, entries);
        writer.key(jsonKey);
        writer.writeObject(typeName);
        auto& fields = ptr->jsonFields();
        fields.writeFieldsOnly(writer, std::to_address(ptr));
        writer.endObject();
    }
};

} // namespace rai::json