/// @file ObjectConverter.Converters.cppm
/// @brief Container と columnar converter の分離実装。

module;
#include <concepts>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <ranges>
#include <stdexcept>

export module rai.serialization.core:container_converter;

import :format_io;
import :object_serializer;
import :object_converter;
import rai.collection.sorted_hash_array_map;
import rai.serialization.token_manager;
import rai.serialization.json;

namespace rai::serialization {

// ******************************************************************************** コンテナ変換方法共通

/// @brief 型 `T` に応じた既定のコンバータを返すユーティリティの前方宣言。
/// @tparam T 変換対象型。
/// @return 型 `T` に対応する既定のコンバータへの参照。
export template <typename T>
constexpr auto& getConverter();

/// @brief ObjectSerializer派生クラスが指定した型のフィールドシリアライザであるか判定するconcept。
/// @tparam Serializer 判定対象のシリアライザ型。
/// @tparam ObjectType フィールド所有者型。
template <typename Serializer, typename ObjectType>
concept IsFieldsObjectSerializer = requires(const Serializer& s,
        std::remove_cvref_t<ObjectType>& item) {
        { s.size() } -> std::same_as<std::size_t>;
        { s.getFieldName(std::size_t{}) } -> std::same_as<std::string_view>;
        { s.writeFieldAt(std::size_t{}, std::declval<FormatWriter&>(),
            std::declval<const std::remove_cvref_t<ObjectType>&>()) };
        { s.readFieldAt(std::size_t{}, std::declval<FormatReader&>(), item) };
        { s.applyMissingAt(std::size_t{}, item) };
    };

/// @brief ObjectSerializer派生クラスが指定した型のフィールドシリアライザであるか判定するconcept（getFieldIndexも持つこと）。
/// @tparam Serializer 判定対象のシリアライザ型。
/// @tparam ObjectType フィールド所有者型。
template <typename Serializer, typename ObjectType>
concept IsFieldsObjectSerializerFor = IsFieldsObjectSerializer<Serializer, ObjectType>
    && requires(const Serializer& s, std::remove_cvref_t<ObjectType>& item) {
        { s.getFieldIndex(std::string_view{}) } -> std::same_as<std::size_t>;
    };

/// @brief スカラー値またはフィールドシリアライザのいずれかであることを判定するconcept。
/// @tparam T 判定対象の型。
/// @tparam Serializer シリアライザ型。
template <typename T, typename Serializer>
concept IsScalarOrFieldsSerializer = IsScalarValue<T> || IsFieldsObjectSerializerFor<Serializer, T>;

/// @brief 2要素ペア型かどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsPairLike = requires {
    typename T::first_type;
    typename T::second_type;
};

/// @brief マップ型コンテナかどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsMapLikeContainer = IsContainer<T>
    && IsPairLike<std::remove_cvref_t<std::ranges::range_value_t<T>>>;

/// @brief コンテナに要素を追加するヘルパー。
/// @tparam Container 追加先コンテナ。
/// @tparam Element 追加する要素の型。
/// @param container 追加先コンテナ。
/// @param element 追加する要素。
template <typename Container, typename Element>
constexpr void insertContainerElement(Container& container, Element&& element) {
    if constexpr (requires(Container& c, Element&& v) {
            c.push_back(std::forward<Element>(v));
        }) {
        container.push_back(std::forward<Element>(element));
    }
    else if constexpr (requires(Container& c, Element&& v) {
            c.insert(std::forward<Element>(v));
        }) {
        container.insert(std::forward<Element>(element));
    }
    else {
        static_assert(false,
            "insertContainerElement: container must support push_back or insert");
    }
}

/// @brief カラム型シリアライズ時のフィールド順序と欠落フィールド情報を保持する構造体。
/// @details fieldOrderは実際のフィールド順序、missingFieldsはデータに存在しないフィールドのインデックスを格納する。
struct ColumnarFields {
    /// @brief 実際のフィールド順序
    std::vector<std::size_t> fieldOrder;
    /// @brief データに存在しないフィールドのインデックス
    std::vector<std::size_t> missingFields;
};

/// @brief フィールド順序と欠落フィールドを読み取る。
/// @tparam SerializerType フィールドシリアライザ型。
/// @param parser JSONパーサ。
/// @param serializer フィールドシリアライザ。
/// @return フィールド順序と欠落フィールド情報。
template <typename SerializerType>
ColumnarFields parseFieldOrder(FormatReader& parser, const SerializerType& serializer) {
    if (parser.nextTokenType() != JsonTokenType::StartArray) {
        throw std::runtime_error("ColumnarContainerConverter: field header must be an array");
    }

    parser.startArray();
    const std::size_t fieldCount = serializer.size();
    std::vector<bool> seen(fieldCount, false);
    std::vector<std::size_t> fieldOrder;
    while (!parser.nextIsEndArray()) {
        std::string name;
        parser.readTo(name);
        const std::size_t fieldIndex = serializer.getFieldIndex(name);
        if (seen[fieldIndex]) {
            throw std::runtime_error(std::string("ColumnarContainerConverter: duplicate column name '") + name + "'");
        }
        seen[fieldIndex] = true;
        fieldOrder.push_back(fieldIndex);
    }
    parser.endArray();

    std::vector<std::size_t> missingFields;
    missingFields.reserve(fieldCount - fieldOrder.size());
    for (std::size_t index = 0; index < fieldCount; ++index) {
        if (!seen[index]) {
            missingFields.push_back(index);
        }
    }
    return ColumnarFields{std::move(fieldOrder), std::move(missingFields)};
}

/// @brief 配列項目を読み取った結果をオブジェクトへ復元する。
/// @tparam T 出力オブジェクト型。
/// @tparam SerializerType フィールドシリアライザ型。
/// @param parser JSONパーサ。
/// @param serializer フィールドシリアライザ。
/// @param fields フィールド順序と欠落フィールド情報。
/// @return 復元されたオブジェクト。
template <typename T, typename SerializerType>
T readObjectRow(FormatReader& parser,
                const SerializerType& serializer,
                const ColumnarFields& fields) {
    parser.startArray();

    T out{};
    std::size_t readCount = 0;
    for (std::size_t fieldIndex : fields.fieldOrder) {
        if (parser.nextIsEndArray()) {
            break;
        }
        serializer.readFieldAt(fieldIndex, parser, out);
        ++readCount;
    }
    while (!parser.nextIsEndArray()) {
        parser.skipValue();
    }
    parser.endArray();

    for (std::size_t i = readCount; i < fields.fieldOrder.size(); ++i) {
        serializer.applyMissingAt(fields.fieldOrder[i], out);
    }
    for (std::size_t missingIndex : fields.missingFields) {
        serializer.applyMissingAt(missingIndex, out);
    }
    return out;
}

// ******************************************************************************** 一般コンテナ

/// @brief 一般的なコンテナ型のシリアライズ・デシリアライズを行うコンバータ。
/// @tparam Container 対象となるコンテナ型。
/// @tparam ElementConverter 要素変換用コンバータ型。
export template <typename Container, typename ElementConverter>
struct ContainerConverter {
    static_assert(IsContainer<Container>,
        "ContainerConverter requires Container to be a container type");
    using Value = Container;
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static_assert(IsObjectConverter<ElementConverter, Element>,
        "ElementConverter must satisfy IsObjectConverter for container element type");
    using ElementConverterT = std::remove_cvref_t<ElementConverter>;
    static_assert(std::is_same_v<typename ElementConverterT::Value, Element>,
        "ElementConverter::Value must match container element type");

    /// @brief コンバータを生成する。
    /// @param elemConv 要素変換用コンバータ。
    constexpr explicit ContainerConverter(const ElementConverter& elemConv)
        : elementConverter_(elemConv) {
    }

    /// @brief コンテナをJSON配列として書き出す。
    /// @param writer 出力先JSONライター。
    /// @param range シリアライズ対象コンテナ。
    void write(JsonWriter& writer, const Container& range) const {
        writer.startArray();
        for (const auto& e : range) {
            elementConverter_.write(writer, e);
        }
        writer.endArray();
    }

    /// @brief JSON配列からコンテナを復元する。
    /// @param parser 入力JSONパーサ。
    /// @return 復元されたコンテナ。
    void read(JsonParser& parser, Container& out) const {
        parser.startArray();
        while (!parser.nextIsEndArray()) {
            Element elem{};
            elementConverter_.read(parser, elem);
            insertContainerElement(out, std::move(elem));
        }
        parser.endArray();
    }

private:
    /// @brief 要素変換用コンバータへの参照
    ElementConverterT elementConverter_;
};

/// @brief コンテナ型に対応する既定の ContainerConverter を返す。
/// @tparam Container コンテナ型
/// @return ContainerConverter の参照
export template <typename Container>
constexpr const auto& getContainerConverter() {
    using Elem = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    const auto& elementConverter = getConverter<Elem>();
    using ElemConv = std::remove_cvref_t<decltype(elementConverter)>;
    static ContainerConverter<Container, ElemConv> converter(elementConverter);
    return converter;
}

/// @brief 明示的な要素コンバータから `ContainerConverter` を作成する。
/// @tparam Container コンテナ型
/// @tparam ElementConverter 要素コンバータ型
/// @param elemConv 要素コンバータ
export template <typename Container, typename ElementConverter>
constexpr auto getContainerConverter(const ElementConverter& elemConv) {
    return ContainerConverter<Container, ElementConverter>(elemConv);
}

// ******************************************************************************** 二重配列表形式

/// @brief 二重配列での表形式（カラム名配列＋値配列）でJSON変換するConverter。
/// @tparam Container コンテナ型。
/// @tparam Serializer フィールドシリアライザ型。
export template <typename Container, typename Serializer>
class ColumnarContainerConverter {
public:
    using Value = Container;
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;

    static_assert(std::is_same_v<typename Serializer::Owner, Element>,
        "Serializer::Owner must match container element type");
    static_assert(IsFieldsObjectSerializer<Serializer, Element>,
        "Serializer must satisfy IsFieldsObjectSerializer for the element type");

    explicit ColumnarContainerConverter(const Serializer& serializer)
        : serializer_(serializer) {}

    /// @brief 配列をカラム名リスト＋値リスト形式でJSON出力。
    void write(FormatWriter& writer, const Value& value) const {
        writer.startArray();
        // カラム名リスト
        writer.startArray();
        for (std::size_t i = 0; i < serializer_.size(); ++i) {
            writer.writeObject(serializer_.getFieldName(i));
        }
        writer.endArray();
        // 各行データ
        for (const auto& item : value) {
            writer.startArray();
            for (std::size_t i = 0; i < serializer_.size(); ++i) {
                serializer_.writeFieldAt(i, writer, item);
            }
            writer.endArray();
        }
        writer.endArray();
    }

    /// @brief JSON配列から配列を復元。
    void read(FormatReader& parser, Value& out) const {
        parser.startArray();
        auto fields = parseFieldOrder(parser, serializer_);

        // 各行データ
        while (!parser.nextIsEndArray()) {
            insertContainerElement(out,
                readObjectRow<Element>(parser, serializer_, fields));
        }
        parser.endArray();
    }

private:
    const Serializer& serializer_;
};

/// @brief 要素の変換方法を指定して ColumnarContainerConverter<std::vector<T>, Serializer> を作成する。
/// @tparam T 配列要素型
/// @tparam Serializer ObjectSerializer派生型
export template <typename T, typename Serializer>
requires (!IsContainer<T>)
constexpr auto getColumnarContainerConverter(const Serializer& serializer) {
    return ColumnarContainerConverter<std::vector<T>, Serializer>{serializer};
}

/// @brief コンテナ型を指定して ColumnarContainerConverter を作成する。
/// @tparam Container コンテナ型
/// @tparam Serializer ObjectSerializer派生型
export template <typename Container, typename Serializer>
    requires IsContainer<Container>
constexpr auto getColumnarContainerConverter(const Serializer& serializer) {
    return ColumnarContainerConverter<Container, Serializer>{serializer};
}

// ******************************************************************************** 通常のJSON配列形式のmap

/// @brief map-like container を key/value ペアの JSON 配列として変換する Converter。
/// @details key と value の両方に任意の ObjectConverter を指定できる。
///          出力形式は [[key, value], ...]。
export template <typename Container, typename KeyConverter, typename ValueConverter>
struct MapConverter {
    using Value = Container;
    using KeyType = std::remove_cvref_t<typename Container::key_type>;
    using ValueType = std::remove_cvref_t<typename Container::mapped_type>;
    using KeyConverterType = std::remove_cvref_t<KeyConverter>;
    using ValueConverterType = std::remove_cvref_t<ValueConverter>;

    static_assert(IsObjectConverter<KeyConverterType, KeyType>,
        "MapConverter requires KeyConverter to satisfy IsObjectConverter for key type");
    static_assert(IsObjectConverter<ValueConverterType, ValueType>,
        "MapConverter requires ValueConverter to satisfy IsObjectConverter for value type");

    constexpr explicit MapConverter(
        const KeyConverterType& keyConverter,
        const ValueConverterType& valueConverter)
        : keyConverter_(&keyConverter)
        , valueConverter_(&valueConverter) {}

    void write(FormatWriter& writer, const Value& value) const {
        writer.startArray();
        for (const auto& item : value) {
            writer.startArray();
            keyConverter_->write(writer, item.first);
            valueConverter_->write(writer, item.second);
            writer.endArray();
        }
        writer.endArray();
    }

    void read(FormatReader& parser, Value& out) const {
        parser.startArray();
        while (!parser.nextIsEndArray()) {
            parser.startArray();
            KeyType key{};
            keyConverter_->read(parser, key);
            ValueType value{};
            valueConverter_->read(parser, value);
            while (!parser.nextIsEndArray()) {
                parser.skipValue();
            }
            parser.endArray();
            insertContainerElement(out, std::make_pair(std::move(key), std::move(value)));
        }
        parser.endArray();
    }

private:
    const KeyConverterType* keyConverter_{};
    const ValueConverterType* valueConverter_{};
};

/// @brief key と value の converter を明示して MapConverter を作成する。
export template <typename Container, typename KeyConverter, typename ValueConverter>
constexpr auto getMapConverter(
    const KeyConverter& keyConverter, const ValueConverter& valueConverter) {
    return MapConverter<Container, KeyConverter, ValueConverter>(
        keyConverter, valueConverter);
}

/// @brief key の既定 converter と value 用 converter から MapConverter を作成する。
export template <typename Container, typename ValueConverter>
constexpr auto getMapConverter(const ValueConverter& valueConverter) {
    using KeyType = std::remove_cvref_t<typename Container::key_type>;
    const auto& keyConverter = getConverter<KeyType>();
    using KeyConverterType = std::remove_cvref_t<decltype(keyConverter)>;
    return MapConverter<Container, KeyConverterType, ValueConverter>(
        keyConverter, valueConverter);
}

/// @brief key と value の既定 converter を使って MapConverter を作成する。
export template <typename Container>
constexpr const auto& getMapConverter() {
    using KeyType = std::remove_cvref_t<typename Container::key_type>;
    using ValueType = std::remove_cvref_t<typename Container::mapped_type>;
    const auto& keyConverter = getConverter<KeyType>();
    const auto& valueConverter = getConverter<ValueType>();
    using KeyConverterType = std::remove_cvref_t<decltype(keyConverter)>;
    using ValueConverterType = std::remove_cvref_t<decltype(valueConverter)>;
    static const MapConverter<Container, KeyConverterType, ValueConverterType> converter(
        keyConverter, valueConverter);
    return converter;
}

// ******************************************************************************** 三重配列形式のmap

/// @brief スカラー値を表すプレースホルダー。
export struct ScalarSerializer {
    static ScalarSerializer instance;
};
ScalarSerializer ScalarSerializer::instance;

/// @brief map型をJSON表形式で変換するConverter。
/// @tparam Container map型コンテナ（map, unordered_map, multimap, unordered_multimap）。
/// @tparam KeySerializer キー用シリアライザ。
/// @tparam ValueSerializer 値用シリアライザ。
export template <typename Container, typename KeySerializer, typename ValueSerializer>
struct ColumnarMapConverter {
    using Value = Container;
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    using KeyType = std::remove_cvref_t<std::remove_const_t<typename Element::first_type>>;
    using ValueType = std::remove_cvref_t<typename Element::second_type>;
    using KeySerializerType = std::remove_cvref_t<KeySerializer>;
    using ValueSerializerType = std::remove_cvref_t<ValueSerializer>;

    static_assert(IsMapLikeContainer<Container>,
        "ColumnarMapConverter requires Container to be a map-like container");
    static_assert(IsScalarOrFieldsSerializer<KeyType, KeySerializerType>,
        "ColumnarMapConverter requires KeySerializer to be a FieldsObjectSerializer for key type or scalar key type");
    static_assert(IsScalarOrFieldsSerializer<ValueType, ValueSerializerType>,
        "ColumnarMapConverter requires ValueSerializer to be a FieldsObjectSerializer for value type or scalar value type");

    explicit ColumnarMapConverter(
        const KeySerializerType& keySerializer, const ValueSerializerType& valueSerializer)
        : keySerializer_(keySerializer)
        , valueSerializer_(valueSerializer) {
    }

    void write(FormatWriter& writer, const Value& value) const {
        writer.startArray();
        writer.startArray();
        writeHeaderItem<KeyType>(writer, keySerializer_, "Key");
        writeHeaderItem<ValueType>(writer, valueSerializer_, "Value");
        writer.endArray();

        for (const auto& item : value) {
            writer.startArray();
            writeElement(writer, keySerializer_, item.first);
            writeElement(writer, valueSerializer_, item.second);
            writer.endArray();
        }

        writer.endArray();
    }

private:
    template <typename T, typename SerializerType>
    static void writeHeaderItem(
        FormatWriter& writer, const SerializerType& serializer, const char* scalarName) {
        if constexpr (IsScalarValue<T>) {
            writer.writeObject(std::string_view(scalarName));
        } else {
            writer.startArray();
            for (std::size_t i = 0; i < serializer.size(); ++i) {
                writer.writeObject(serializer.getFieldName(i));
            }
            writer.endArray();
        }
    }

    template <typename T, typename SerializerType>
    static void writeElement(
        FormatWriter& writer, const SerializerType& serializer, const T& value) {
        if constexpr (IsScalarValue<T>) {
            getConverter<T>().write(writer, value);
        } else {
            writer.startArray();
            for (std::size_t i = 0; i < serializer.size(); ++i) {
                serializer.writeFieldAt(i, writer, value);
            }
            writer.endArray();
        }
    }

public:
    void read(FormatReader& parser, Value& out) const {
        parser.startArray();

        parser.startArray();
        const auto keySchema = parseSchema<KeyType>(parser, keySerializer_, "Key");
        const auto valueSchema = parseSchema<ValueType>(parser, valueSerializer_, "Value");
        while (!parser.nextIsEndArray()) {
            parser.skipValue();
        }
        parser.endArray();

        while (!parser.nextIsEndArray()) {
            parser.startArray();
            KeyType key = readElement<KeyType>(parser, keySerializer_, keySchema);
            ValueType value = readElement<ValueType>(parser, valueSerializer_, valueSchema);
            while (!parser.nextIsEndArray()) {
                parser.skipValue();
            }
            parser.endArray();
            insertContainerElement(out, std::make_pair(std::move(key), std::move(value)));
        }

        parser.endArray();
    }

private:
    template <typename T, typename SerializerType>
    static ColumnarFields parseSchema(
        FormatReader& parser, const SerializerType& serializer, const char* scalarName) {
        if constexpr (IsScalarValue<T>) {
            parseScalarSchema<T>(parser, scalarName);
            return ColumnarFields{};
        } else {
            return parseObjectSchema<T>(parser, serializer, scalarName);
        }
    }

    template <typename T>
    static void parseScalarSchema(FormatReader& parser, const char* scalarName) {
        std::string ignored;
        parser.readTo(ignored);
    }

    template <typename T, typename SerializerType>
    static ColumnarFields parseObjectSchema(
        FormatReader& parser, const SerializerType& serializer, const char* scalarName) {
        if (parser.nextTokenType() != JsonTokenType::StartArray) {
            throw std::runtime_error(std::string("ColumnarMapConverter: object ") + scalarName
                + " header must be an array");
        }
        return parseFieldOrder(parser, serializer);
    }

    template <typename T, typename SerializerType>
    static T readElement(
        FormatReader& parser, const SerializerType& serializer, const ColumnarFields& fields) {
        if constexpr (IsScalarValue<T>) {
            if (parser.nextIsEndArray()) {
                return T{};
            }
            T out{};
            getConverter<T>().read(parser, out);
            return out;
        } else {
            return readObjectRow<T>(parser, serializer, fields);
        }
    }

    KeySerializerType keySerializer_;
    ValueSerializerType valueSerializer_;
};

/// @brief マップ型をJSON表形式に変換するConverterを生成する。
/// @tparam Container マップ型コンテナ。
/// @tparam KeySerializer キー型に対応するフィールドシリアライザ。
/// @tparam ValueSerializer 値型に対応するフィールドシリアライザ。
export template <typename Container, typename KeySerializer, typename ValueSerializer>
constexpr auto getColumnarMapConverter(
    const KeySerializer& keySerializer, const ValueSerializer& valueSerializer) {
    return ColumnarMapConverter<Container, KeySerializer, ValueSerializer>
    {keySerializer, valueSerializer};
}

/// @brief キーがスカラーで値がスカラーなマップに対応するColumnarMapConverterを返す。
export template <typename Container>
requires IsMapLikeContainer<Container>
constexpr const auto& getColumnarMapConverter() {
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    using KeyType = std::remove_cvref_t<std::remove_const_t<typename Element::first_type>>;
    using ValueType = std::remove_cvref_t<typename Element::second_type>;
    static_assert(IsScalarValue<KeyType>, "getColumnarMapConverter(): key type must be scalar");
    static_assert(IsScalarValue<ValueType>, "getColumnarMapConverter(): value type must be scalar");
    static ColumnarMapConverter<Container, ScalarSerializer, ScalarSerializer> converter
    (ScalarSerializer::instance, ScalarSerializer::instance);
    return converter;
}

} // namespace rai::serialization
