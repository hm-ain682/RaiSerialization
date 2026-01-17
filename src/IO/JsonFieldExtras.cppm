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

export module rai.json.json_field_extras;


import rai.json.json_concepts;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;
import rai.json.json_field;
import rai.json.json_value_io;
import rai.collection.sorted_hash_array_map;

namespace rai::json {

// ------------------------- JsonEnumField, Polymorphic and Token dispatch -------------------------

export template <typename EnumType>
struct EnumEntry {
    EnumType value;   ///< Enum値。
    const char* name; ///< 対応する文字列名。
};

/// @brief Enum型のフィールド用に特化したJsonField派生クラス。
/// @tparam MemberPtr Enumメンバー変数へのポインタ。
/// @tparam Entries Enumと文字列のマッピング配列への参照。
export template <typename MemberPtrType, std::size_t N = 0>
struct JsonEnumField : JsonField<MemberPtrType> {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using ValueType = typename Traits::ValueType;
    static_assert(std::is_enum_v<ValueType>, "JsonEnumField requires enum type");

    /// @brief Enum用フィールドのコンストラクタ（エントリ配列を受け取る）
    /// @param memberPtr ポインタメンバ
    /// @param keyName JSONキー名
    /// @param entries エントリ配列
    constexpr explicit JsonEnumField(MemberPtrType memberPtr, const char* keyName,
        const EnumEntry<ValueType> (&entries)[N], bool req = false)
        : JsonField<MemberPtrType>(memberPtr, keyName, req) {

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

    /// @brief Enum値をJSONに書き出す。
    /// @param writer JsonWriterの参照。
    /// @param value 変換対象のenum値。
    /// @throws std::runtime_error マッピングが存在しない場合。
    void toJson(JsonWriter& writer, const ValueType& value) const {
        // エントリが空の場合は常に失敗
        if constexpr (N == 0) {
            throw std::runtime_error("Failed to convert enum to string");
        }
        const auto* found = valueToName_.findValue(value);
        if (found) {
            writer.writeObject(*found);
            return;
        }
        // マッピングに存在しない値の場合
        throw std::runtime_error("Failed to convert enum to string");
    }

    /// @brief JSONからEnum値を読み取る。
    /// @param parser JsonParserの参照。
    /// @return 変換されたenum値。
    /// @throws std::runtime_error マッピングに存在しない文字列の場合。
    /// @note 内部で文字列を読み取り、enumエントリで検索する。
    ValueType fromJson(JsonParser& parser) const {
        std::string jsonValue;
        parser.readTo(jsonValue);

        // エントリが空の場合は常に失敗
        if constexpr (N == 0) {
            throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
        }
        const auto* found = nameToValue_.findValue(jsonValue);
        if (found) {
            return *found;
        }
        // マッピングに存在しない文字列の場合
        throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
    }

private:
    /// @note エントリはSortedHashArrayMapに保持する（キー->値 / 値->キーで2方向検索を高速化）
    collection::SortedHashArrayMap<std::string_view, ValueType, N> nameToValue_{}; ///< 名前からenum値へのマップ。
    collection::SortedHashArrayMap<ValueType, std::string_view, N> valueToName_{}; ///< enum値から名前へのマップ。
};

/// @brief ポリモーフィック型用のファクトリ関数型（ポインタ型を返す）。
export template <typename PtrType>
    requires SmartOrRawPointer<PtrType>
using PolymorphicTypeFactory = std::function<PtrType()>;

/// @brief ポリモーフィックオブジェクト1つ分を読み取るヘルパー関数。
/// @tparam PtrType ポインタ型（unique_ptr/shared_ptr/生ポインタ）。
/// @param parser JsonParserの参照。
/// @param entriesMap 型名からファクトリ関数へのマッピング。
/// @param jsonKey 型判別用のJSONキー名。
/// @return 読み取ったオブジェクトのポインタ。または型キーが見つからない／未知の型名の場合はnullptr。
export template <typename PtrType>
    requires SmartOrRawPointer<PtrType>
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
/// @tparam PtrType ポインタ型（unique_ptr/shared_ptr/生ポインタ）。
/// @param parser JsonParserの参照。
/// @param entriesMap 型名からファクトリ関数へのマッピング。
/// @param jsonKey 型判別用のJSONキー名。
/// @return 読み取ったオブジェクトのポインタ。nullの場合はnullptr。
template <typename PtrType>
    requires SmartOrRawPointer<PtrType>
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
    requires SmartOrRawPointer<typename MemberPointerTraits<MemberPtrType>::ValueType>
struct JsonPolymorphicField : JsonField<MemberPtrType> {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using ValueType = typename Traits::ValueType;
    using Base = JsonField<MemberPtrType>;
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
    const char* jsonKey_; ///< JSON内で型を判別するためのキー名。
};

/// @brief 配列形式のJSONを読み書きする汎用フィールド。
/// @tparam MemberPtrType コンテナ型のメンバー変数へのポインタ。
/// @details push_back または insert を持つコンテナに対応する。
export template <typename MemberPtrType>
    requires std::ranges::range<typename MemberPointerTraits<MemberPtrType>::ValueType>
struct JsonSetField : JsonField<MemberPtrType> {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using ValueType = typename Traits::ValueType;
    using Base = JsonField<MemberPtrType>;
    using ElementType = std::ranges::range_value_t<ValueType>; ///< コンテナの要素型。

    /// @brief コンストラクタ。
    /// @param memberPtr メンバー変数へのポインタ。
    /// @param keyName JSONキー名。
    /// @param req 必須フィールドかどうか。
    constexpr explicit JsonSetField(MemberPtrType memberPtr, const char* keyName,
        bool req = false)
        : Base(memberPtr, keyName, req) {}

    /// @brief JsonParserから配列を読み取る。
    /// @param parser JsonParserの参照。
    /// @return 読み取ったコンテナ。
    ValueType fromJson(JsonParser& parser) const {
        ValueType out{};
        parser.startArray();
        while (!parser.nextIsEndArray()) {
            if constexpr (std::is_same_v<ElementType, std::string>) {
                std::string s;
                parser.readTo(s);
                addElement(out, std::move(s));
            } else {
                auto elem = ::rai::json::value_io::template readValue<ElementType>(parser);
                addElement(out, std::move(elem));
            }
        }
        parser.endArray();
        return out;
    }

    /// @brief JsonWriterに配列を書き出す。
    /// @param writer JsonWriterの参照。
    /// @param container 書き出すコンテナ。
    void toJson(JsonWriter& writer, const ValueType& container) const {
        writer.startArray();
        for (const auto& elem : container) {
            ::rai::json::value_io::template writeValue<ElementType>(writer, elem);
        }
        writer.endArray();
    }

private:
    /// @brief コンテナへ要素を追加する。
    /// @param container 追加先のコンテナ。
    /// @param elem 追加する要素。
    static void addElement(ValueType& container, ElementType elem) {
        if constexpr (requires(ValueType& c, ElementType&& v) { c.push_back(std::move(v)); }) {
            container.push_back(std::move(elem));
        }
        else if constexpr (requires(ValueType& c, ElementType&& v) { c.insert(std::move(v)); }) {
            container.insert(std::move(elem));
        }
        else {
            static_assert(AlwaysFalse<ValueType>, "Container must support push_back or insert");
        }
    }
};

/// @brief JsonSetFieldを生成するヘルパー関数。
/// @tparam MemberPtrType メンバーポインタ型。
/// @param memberPtr メンバー変数へのポインタ。
/// @param keyName JSONキー名。
/// @param req 必須フィールドかどうか。
/// @return 生成されたJsonSetField。
export template <typename MemberPtrType>
constexpr auto makeJsonSetField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    return JsonSetField<MemberPtrType>(memberPtr, keyName, req);
}

/// @brief JsonField を生成するヘルパー関数（CTAD 回避用）
export template <typename MemberPtrType>
constexpr auto makeJsonField(MemberPtrType memberPtr, const char* keyName, bool req = false) {
    return JsonField<MemberPtrType>(memberPtr, keyName, req);
}

/// @brief ポリモーフィックな配列（vector<std::unique_ptr<BaseType>>）用のフィールド。
/// @tparam MemberPtrType ポインタのvector型のメンバー変数へのポインタ。
/// @details JsonSetFieldを継承し、ポリモーフィック型用のデフォルト動作を提供する。
export template <typename MemberPtrType>
    requires VectorOfPointers<typename MemberPointerTraits<MemberPtrType>::ValueType>
struct JsonPolymorphicArrayField : JsonField<MemberPtrType> {
    using Traits = MemberPointerTraits<MemberPtrType>;
    using ValueType = typename Traits::ValueType;
    using Base = JsonField<MemberPtrType>;
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

// ******************************************************************************** トークン種別ディスパッチフィールド

/// @brief JsonTokenTypeの総数。
/// @note この値はJsonTokenType enumの要素数と一致している必要がある。
export inline constexpr std::size_t JsonTokenTypeCount = 12;

/// @brief JSONからの読み取り用コンバータ型。
/// @tparam ValueType 変換対象の値型。
/// @note 配列添え字がJsonTokenTypeに対応する。
export template <typename ValueType>
using FromJsonEntry = std::function<ValueType(JsonParser&)>;

/// @brief JSONトークン種別に応じて変換処理を切り替えるフィールド。
/// @tparam MemberPtrType メンバー変数へのポインタ型。
/// @details fromJsonでは次のトークン種別に対応するコンバータを使用して値を読み取る。
///          toJsonでは指定されたコンバータを使用して書き出す。
export template <typename MemberPtrType>
struct JsonTokenDispatchField : JsonField<MemberPtrType> {
    using Base = JsonField<MemberPtrType>;
    using typename Base::ValueType;
    using ToConverter = std::function<void(JsonWriter&, const ValueType&)>;

    /// @brief コンストラクタ。
    /// @param memberPtr メンバーポインタ。
    /// @param keyName JSONキー名。
    /// @param fromEntries トークン種別をインデックスとする読み取りコンバータ配列（要素数はJsonTokenTypeCount以下）。
    /// @param toConverter 書き出し用コンバータ関数。省略時はJsonWriter::writeObjectで書き出す。
    /// @param req 必須フィールドかどうか。
    /// @note fromEntries[i]はJsonTokenType(i)に対応するコンバータ。対応しないインデックスにはnullptrを設定可能。
    template <std::size_t FromN>
    explicit JsonTokenDispatchField(MemberPtrType memberPtr, const char* keyName,
        const std::array<FromJsonEntry<ValueType>, FromN>& fromEntries,
        ToConverter toConverter = defaultToConverter(),
        bool req = false)
        : Base(memberPtr, keyName, req), toConverter_(std::move(toConverter)) {
        static_assert(FromN <= JsonTokenTypeCount);
        // まず全要素を例外を投げる関数で初期化
        for (std::size_t i = 0; i < JsonTokenTypeCount; ++i) {
            fromEntries_[i] = [](JsonParser&) -> ValueType {
                throw std::runtime_error("No converter found for token type");
            };
        }
        // fromEntriesの有効なエントリをコピー（配列添え字＝トークン種別）
        for (std::size_t i = 0; i < FromN; ++i) {
            if (fromEntries[i]) {
                fromEntries_[i] = fromEntries[i];
            }
        }
    }

    /// @brief JSONから値を読み取る。
    /// @param parser JsonParserの参照。
    /// @return 読み取った値。
    /// @throws std::runtime_error 対応するコンバータが見つからない場合。
    ValueType fromJson(JsonParser& parser) const {
        // トークン種別をインデックスとして直接アクセス
        std::size_t index = static_cast<std::size_t>(parser.nextTokenType());
        return fromEntries_[index](parser);
    }

    /// @brief JSONに値を書き出す。
    /// @param writer JsonWriterの参照。
    /// @param value 書き出す値。
    void toJson(JsonWriter& writer, const ValueType& value) const {
        toConverter_(writer, value);
    }

    /// @brief 書き出し用コンバータを設定する。
    /// @param toConverter 設定するコンバータ関数。
    void setToConverter(ToConverter toConverter) {
        toConverter_ = std::move(toConverter);
    }

    /// @brief 既定の書き出し用コンバータ。
    /// @return JsonWriter::writeObjectで値を書き出すコンバータ関数。
    static ToConverter defaultToConverter() {
        return [](JsonWriter& writer, const ValueType& value) {
            writer.writeObject(value);
        };
    }

private:
    std::array<std::function<ValueType(JsonParser&)>, JsonTokenTypeCount> fromEntries_{}; ///< トークン種別をインデックスとする読み取り関数配列。
    ToConverter toConverter_; ///< 書き出し用コンバータ関数。
};

}  // namespace rai::json

