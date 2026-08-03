/// @file ObjectConverter.cppm
/// @brief JSONの値変換コンバータ群を提供する。

module;
#include <memory>
#include <concepts>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <stdexcept>
#include <array>
#include <optional>
#include <variant>
#include <functional>
#include <ranges>
#include <span>

export module rai.serialization.core:object_converter;

import :format_io;
import :object_serializer;
import rai.collection.sorted_hash_array_map;
import rai.serialization.token_manager;
import rai.serialization.json;

export namespace rai::serialization {

// ******************************************************************************** concept

/// @brief フォーマットへの書き出しと読み込みを行うコンバータに要求される条件を定義する concept。
/// @tparam Converter コンバータ型
/// @tparam Value コンバータが扱う値の型
template <typename Converter, typename Value>
concept IsObjectConverter = std::is_class_v<Converter>
    && requires { typename Converter::Value; }
    && std::is_same_v<typename Converter::Value, Value>
    && requires(const Converter& converter, FormatWriter& writer, const Value& value) {
        converter.write(writer, value);
    }
    && requires(const Converter& converter, FormatReader& parser, Value& value) {
        { converter.read(parser, value) } -> std::same_as<std::size_t>;
    };

/// @brief readメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasReadFormatCore = requires(T& obj, FormatReader& parser) {
    { obj.read(parser) } -> std::same_as<std::size_t>;
};

/// @brief writeメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasWriteFormatCore = requires(const T& obj, FormatWriter& writer) {
    { obj.write(writer) } -> std::same_as<void>;
};

/// @brief std::optional 型かどうかを判定する trait。
/// @tparam T 判定対象の型。
template <typename T>
struct IsStdOptionalCore : std::false_type {};

/// @brief std::optional 型かどうかを判定する trait の特殊化。
/// @tparam T optional の要素型。
template <typename T>
struct IsStdOptionalCore<std::optional<T>> : std::true_type {};

/// @brief std::optional 型かどうかを判定する concept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsStdOptional = IsStdOptionalCore<std::remove_cvref_t<T>>::value;

/// @brief 文字列系型かどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept LikesString = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>;

/// @brief string 系を除くレンジ（配列/コンテナ）を表す concept。
/// @details std::ranges::range を満たし、かつ `LikesString` を除外することで
///          `std::string` を配列として誤判定しないようにします。
/// @tparam T 判定対象の型。
template<typename T>
concept IsContainer = std::ranges::range<T> && !LikesString<T>;

/// @brief 型 `T` に応じた既定のコンバータを返すユーティリティの前方宣言。
/// @tparam T 変換対象型。
/// @return 型 `T` に対応する既定コンバータへの参照。
template <typename T>
constexpr auto& getConverter();

// ******************************************************************************** 基本型用変換方法

/// @brief プリミティブ型（int, double, bool など）かどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsFundamentalValue = std::is_fundamental_v<T>;

/// @brief スカラー値型かどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsScalarValue = IsFundamentalValue<T> || std::same_as<T, std::string>;

/// @brief プリミティブ型、文字列型の変換方法。
template <typename T>
struct FundamentalConverter {
    static_assert(IsScalarValue<T>, "FundamentalConverter requires T to be a fundamental JSON value or std::string");
    using Value = T;

    void write(JsonWriter& writer, const T& value) const {
        writer.writeObject(value);
    }

    std::size_t read(JsonParser& parser, T& out) const {
        parser.readTo(out);
        return parser.nextPosition();
    }
};

/// @brief 指定されたObjectSerializer派生クラスによる任意型の変換方法。
/// @tparam T 変換対象型
/// @tparam Serializer ObjectSerializer派生クラス
template <typename T, typename Serializer>
struct ObjectSerializerConverter {
    using Value = T;

    explicit ObjectSerializerConverter(const Serializer& serializer)
        : serializer_(serializer) {}

    void write(JsonWriter& writer, const Value& value) const {
        writer.startObject();
        serializer_.writeFields(writer, static_cast<const void*>(&value));
        writer.endObject();
    }

    std::size_t read(JsonParser& parser, Value& out) const {
        parser.startObject();
        serializer_.readFields(parser, static_cast<void*>(&out));
        parser.endObject();
        return parser.nextPosition();
    }

private:
    const Serializer& serializer_;
};

/// @brief ObjectSerializer派生を指定して ObjectSerializerConverter を作成する。
/// @tparam T 対象オブジェクト型
/// @tparam Serializer ObjectSerializer派生クラス
/// @param serializer ObjectSerializer派生クラスのインスタンス
/// @return ObjectSerializerConverter のインスタンス
template <typename T, typename Serializer>
constexpr auto getObjectSerializerConverter(const Serializer& serializer) {
    return ObjectSerializerConverter<T, Serializer>{serializer};
}

/// @brief serializer()メンバー関数を持つかどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept HasSerializer = requires(const T& t) { t.serializer(); };

/// @brief serializer() を直接使う型のコンバータ。
template <typename T>
struct SerializerConverter {
    static_assert(HasSerializer<T> && std::default_initializable<T>,
        "SerializerConverter requires T to have serializer() and be default-initializable");
    using Value = T;

    void write(FormatWriter& writer, const T& obj) const {
        writer.startObject();
        const auto& fields = obj.serializer();
        fields.writeFields(writer, static_cast<const void*>(&obj));
        writer.endObject();
    }

    std::size_t read(FormatReader& parser, T& obj) const {
        parser.startObject();
        auto& fields = obj.serializer();
        fields.readFields(parser, static_cast<void*>(&obj));
        parser.endObject();
        return parser.nextPosition();
    }
};

/// @brief readメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasReadFormat = HasReadFormatCore<T>;

/// @brief writeメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasWriteFormat = HasWriteFormatCore<T>;

/// @brief write/readを持つ型のコンバータ
template <typename T>
struct ReadWriteFormatConverter {
    static_assert(HasReadFormat<T> && HasWriteFormat<T> && std::default_initializable<T>,
        "ReadWriteFormatConverter requires T to have read/write and be default-initializable");
    using Value = T;

    void write(FormatWriter& writer, const T& obj) const {
        obj.write(writer);
    }
    std::size_t read(FormatReader& parser, T& out) const {
        return out.read(parser);
    }
};

// ******************************************************************************** optional用変換方法

/// @brief std::optional 用のコンバータ。
/// @tparam T std::optional 型。
/// @tparam ElementConverter optional の要素型に対するコンバータ。
template <typename T, typename ElementConverter>
struct OptionalConverter {
    static_assert(IsStdOptional<T>, "OptionalConverter requires T to be std::optional<U>");

    using Value = T;
    using Element = typename T::value_type;
    using ElementConverterType = std::remove_cvref_t<ElementConverter>;
    static_assert(IsObjectConverter<ElementConverterType, Element>,
        "OptionalConverter requires ElementConverter to satisfy IsObjectConverter");

    /// @brief 要素コンバータを指定して構築する。
    /// @param converter optional の要素型に対するコンバータ。
    constexpr explicit OptionalConverter(const ElementConverter& converter)
        : elementConverter_(std::cref(converter)) {}

    /// @brief optional値をJSONへ書き出す。
    /// @param writer 出力先ライタ。
    /// @param value 書き出す optional 値。
    void write(JsonWriter& writer, const T& value) const {
        if (!value.has_value()) {
            writer.null();
            return;
        }
        elementConverter_.get().write(writer, *value);
    }

    /// @brief JSONからoptional値を読み込む。
    /// @param parser 入力元パーサ。
    /// @return 読み込んだ optional 値。
    std::size_t read(JsonParser& parser, T& out) const {
        if (parser.nextIsNull()) {
            parser.skipValue();
            out.reset();
            return parser.nextPosition();
        }
        return elementConverter_.get().read(parser, out.emplace());
    }

private:
    /// @brief optional の要素型に対するコンバータ参照。
    std::reference_wrapper<const ElementConverterType> elementConverter_{};
};

/// @brief std::optional 型に対応する既定の `OptionalConverter` を作成する。
/// @tparam T std::optional 型。
/// @return `T` に対応する既定の OptionalConverter 参照。
template <typename T>
constexpr auto& getOptionalConverter() {
    static_assert(IsStdOptional<T>, "getOptionalConverter requires T to be std::optional<U>");

    using Element = typename T::value_type;
    const auto& elementConverter = getConverter<Element>();
    using ElementConverterType = std::remove_cvref_t<decltype(elementConverter)>;
    static const OptionalConverter<T, ElementConverterType> instance{ elementConverter };
    return instance;
}

/// @brief 要素コンバータを指定して `OptionalConverter` を作成する。
/// @tparam T std::optional 型。
/// @tparam ElementConverter 要素コンバータ型。
/// @param elementConverter optional の要素型に対するコンバータ。
/// @return 要素コンバータ指定済みの OptionalConverter。
template <typename T, typename ElementConverter>
constexpr auto getOptionalConverter(const ElementConverter& elementConverter) {
    return OptionalConverter<T, ElementConverter>(elementConverter);
}

/// @brief 標準でサポートする型を判定する。
/// @tparam T 判定対象の型
template <typename T>
concept IsDefaultConverterSupported
    = IsFundamentalValue<T>
    || std::same_as<T, std::string>
    || IsStdOptional<T>
    || HasSerializer<T>
    || (HasReadFormat<T> && HasWriteFormat<T>);

/// @brief 型 `T` に応じた既定のコンバータを返すユーティリティ。
/// @note 基本型、`HasSerializer`、`HasReadFormat`/`HasWriteFormat` を持つ型を自動的に扱い、その他の複雑な型は明確な static_assert で除外します。
template <typename T>
constexpr auto& getConverter() {
    if constexpr (IsFundamentalValue<T> || std::same_as<T, std::string>) {
        static const FundamentalConverter<T> inst{};
        return inst;
    }
    else if constexpr (IsStdOptional<T>) {
        return getOptionalConverter<T>();
    }
    else if constexpr (HasSerializer<T>) {
        static const SerializerConverter<T> inst{};
        return inst;
    }
    else if constexpr (HasReadFormat<T> && HasWriteFormat<T>) {
        static const ReadWriteFormatConverter<T> inst{};
        return inst;
    }
    else {
        static_assert(false,
            "getConverter: unsupported type");
    }
}

// ******************************************************************************** enum用変換方法

// EnumTextMapのように、enum <-> 文字列名の双方向マップを提供する型のconcept。
template <typename Map>
concept IsEnumTextMap
    = requires { typename Map::Enum; }
    && std::is_enum_v<typename Map::Enum>
    && requires(const Map& m, std::string_view s, typename Map::Enum v) {
        { m.fromName(s) } -> std::same_as<std::optional<typename Map::Enum>>;
        { m.toName(v) } -> std::same_as<std::optional<std::string_view>>;
    };

/// @brief EnumEntry は enum 値と文字列名の対応を保持します
template <typename EnumType>
struct EnumEntry {
    EnumType value;   ///< Enum値。
    const char* name; ///< 対応する文字列名。
};

/// @brief EnumEntry を利用して enum <-> name の双方向マップを持つ再利用可能な型。
/// @tparam EnumType enum 型
/// @tparam N エントリ数（静的）
template <typename EnumType, std::size_t N>
struct EnumTextMap {
    using Enum = EnumType;

    /// @brief std::span ベースのコンストラクタ（C配列やstd::arrayからの変換を受け取ります）
    constexpr explicit EnumTextMap(std::span<const EnumEntry<Enum>> entries) {
        if (entries.size() != N) {
            throw std::runtime_error("EnumTextMap(span): size must match template parameter N");
        }
        std::pair<std::string_view, Enum> nv[N];
        for (std::size_t i = 0; i < N; ++i) {
            nv[i] = { entries[i].name, entries[i].value };
        }
        nameToValue_ = collection::SortedHashArrayMap<std::string_view, Enum, N>(nv);

        std::pair<Enum, std::string_view> vn[N];
        for (std::size_t i = 0; i < N; ++i) {
            vn[i] = { entries[i].value, entries[i].name };
        }
        valueToName_ = collection::SortedHashArrayMap<Enum, std::string_view, N>(vn);
    }

    /// @brief 文字列から enum を得る。見つからない場合は nullopt。
    constexpr std::optional<Enum> fromName(std::string_view name) const {
        if (auto p = nameToValue_.findValue(name)) {
            return *p;
        }
        return std::nullopt;
    }

    /// @brief enum から文字列名を得る。見つからない場合は nullopt。
    constexpr std::optional<std::string_view> toName(Enum v) const {
        if (auto p = valueToName_.findValue(v)) {
            return *p;
        }
        return std::nullopt;
    }

private:
    ///! 名前からenum値へのマップ。
    collection::SortedHashArrayMap<std::string_view, Enum, N> nameToValue_{};
    ///! enum値から名前へのマップ。
    collection::SortedHashArrayMap<Enum, std::string_view, N> valueToName_{};
};

/// @brief 列挙型用のコンバータ
/// @tparam MapType EnumTextMap型など
template <typename MapType>
struct EnumConverter {
    static_assert(IsEnumTextMap<MapType>,
        "EnumConverter requires MapType to satisfy IsEnumTextMap");
    using Enum = typename MapType::Enum;
    using Value = Enum;
    
    constexpr explicit EnumConverter(const MapType& map)
        : map_(map) {}

    void write(JsonWriter& writer, const Enum& value) const {
        if (auto name = map_.toName(value)) {
            writer.writeObject(*name);
            return;
        }
        throw std::runtime_error("Failed to convert enum to string");
    }

    std::size_t read(JsonParser& parser, Enum& out) const {
        std::string jsonValue;
        parser.readTo(jsonValue);
        if (auto v = map_.fromName(jsonValue)) {
            out = *v;
            return parser.nextPosition();
        }
        throw std::runtime_error(std::string("Failed to convert string to enum: ") + jsonValue);
    }
private:
    MapType map_;
};

/// @brief C 配列から EnumConverter を構築する。
template <typename Enum, std::size_t N>
constexpr auto getEnumConverter(const EnumEntry<Enum> (&entries)[N]) {
    const EnumTextMap<Enum, N> map{ std::span<const EnumEntry<Enum>, N>(entries) };
    return EnumConverter<EnumTextMap<Enum, N>>(map);
}

/// @brief array から EnumConverter を構築する。
template <typename Enum, std::size_t M>
constexpr auto getEnumConverter(const std::array<EnumEntry<Enum>, M>& entries) {
    const EnumTextMap<Enum, M> map{ std::span<const EnumEntry<Enum>, M>(entries.data(), M) };
    return EnumConverter<EnumTextMap<Enum, M>>(map);
}

/// @brief spanから EnumConverter を構築する。
template <typename Enum, std::size_t N>
constexpr auto getEnumConverter(std::span<const EnumEntry<Enum>, N> entries) {
    const EnumTextMap<Enum, N> map{ entries };
    return EnumConverter<EnumTextMap<Enum, N>>(map);
}

// ******************************************************************************** pointer用変換方法

/// @brief pointer-like 型から参照先の要素型を取り出す。
template <typename T, typename = void>
struct PointerElementType {};

template <typename T>
struct PointerElementType<
    T,
    std::void_t<typename std::pointer_traits<std::remove_cvref_t<T>>::element_type>> {
    using type = std::remove_cv_t<
        typename std::pointer_traits<std::remove_cvref_t<T>>::element_type>;
};

/// @brief pointer-like 型から実ポインタを取得する。
/// @details smart pointer 風の型は get() を使い、生ポインタはそのまま返す。
template <typename T>
    requires std::is_pointer_v<std::remove_cvref_t<T>>
constexpr auto getRawPointer(T& pointer) {
    return pointer;
}

template <typename T>
    requires (!std::is_pointer_v<std::remove_cvref_t<T>>)
        && requires(T& pointer) { pointer.get(); }
constexpr auto getRawPointer(T& pointer) {
    return pointer.get();
}

/// @brief get() で実ポインタを取り出せる pointer-like 型かどうかを判定する。
/// @details 生ポインタは get() を持たないため、getRawPointer() 内で直接扱う。
template <typename T>
concept HasGetRawPointer = requires(T& pointer, const T& constPointer) {
    typename PointerElementType<T>::type;
    { getRawPointer(pointer) } -> std::convertible_to<typename PointerElementType<T>::type*>;
    { getRawPointer(constPointer) } -> std::convertible_to<typename PointerElementType<T>::type*>;
};

/// @brief null を表せて参照先を読み書きできる pointer-like 型かどうかを判定する。
template <typename T>
concept IsPointerLike = HasGetRawPointer<T>
    && std::constructible_from<T, std::nullptr_t>
    && requires(const T& pointer) {
        typename PointerElementType<T>::type;
        { pointer == nullptr } -> std::convertible_to<bool>;
        { pointer != nullptr } -> std::convertible_to<bool>;
        { *pointer } -> std::convertible_to<const typename PointerElementType<T>::type&>;
    };

/// @brief PointerConverter の既定生成方法。
/// @details unique_ptr/shared_ptr は標準の生成関数を使い、それ以外は Element* から
///          Ptr を構築できる場合だけ new Element(...) を渡す。
template <typename Ptr>
struct DefaultPointerFactory {
    using Element = typename PointerElementType<Ptr>::type;
    Ptr operator()(JsonParser& parser, Element&& value) const
        requires std::constructible_from<Ptr, Element*> {
        return Ptr(new Element(std::move(value)));
    }
};

template <typename T, typename Deleter>
struct DefaultPointerFactory<std::unique_ptr<T, Deleter>> {
    std::unique_ptr<T, Deleter> operator()(JsonParser& parser, T&& value) const
        requires std::default_initializable<Deleter> {
        return std::unique_ptr<T, Deleter>(new T(std::move(value)), Deleter{});
    }
};

template <typename T>
struct DefaultPointerFactory<std::shared_ptr<T>> {
    std::shared_ptr<T> operator()(JsonParser& parser, T&& value) const {
        return std::make_shared<T>(std::move(value));
    }
};

/// @brief nullable pointer-like 型のコンバータ。
template <
    typename T,
    typename TargetConverter,
    typename PointerFactory = DefaultPointerFactory<T>>
struct PointerConverter {
    using Value = T;
    using Element = typename PointerElementType<T>::type;
    using ElemConvT = std::remove_cvref_t<TargetConverter>;
    using FactoryT = std::remove_cvref_t<PointerFactory>;
    static_assert(IsPointerLike<T>, "PointerConverter requires T to be a nullable pointer-like type");
    static_assert(IsObjectConverter<ElemConvT, Element>,
        "PointerConverter requires ElementConverter to be an ObjectConverter for element type");
    static_assert(requires(const FactoryT& factory, JsonParser& parser, Element&& element) {
        { factory(parser, std::move(element)) } -> std::same_as<T>;
    }, "PointerConverter requires PointerFactory to create T from Element&&");

    PointerConverter()
        : targetConverter_(std::cref(getConverter<Element>())), pointerFactory_{} {}

    constexpr explicit PointerConverter(const ElemConvT& conv)
        : targetConverter_(std::cref(conv)), pointerFactory_{} {}

    constexpr PointerConverter(const ElemConvT& conv, FactoryT factory)
        : targetConverter_(std::cref(conv)), pointerFactory_(std::move(factory)) {}

    void write(JsonWriter& writer, const T& ptr) const {
        if (ptr == nullptr) {
            writer.null();
            return;
        }
        targetConverter_.get().write(writer, *ptr);
    }

    std::size_t read(JsonParser& parser, T& out) const {
        if (parser.nextIsNull()) {
            parser.skipValue();
            out = nullptr;
            return parser.nextPosition();
        }
        Element elem{};
        targetConverter_.get().read(parser, elem);
        out = pointerFactory_(parser, std::move(elem));
        return parser.nextPosition();
    }

private:
    std::reference_wrapper<const ElemConvT> targetConverter_;
    FactoryT pointerFactory_;
};

/// @brief pointer-like 型のjson変換方法を返す。※インスタンスはstatic。
template <typename T>
constexpr auto& getPointerConverter() {
    using TargetConverter = decltype(getConverter<typename PointerElementType<T>::type>());
    static const PointerConverter<T, TargetConverter> inst{};
    return inst;
}

/// @brief 参照先の変換方法を指定して pointer-like 型のjson変換方法を返す。
template <typename T, typename ElementConverter>
constexpr auto getPointerConverter(const ElementConverter& elementConverter) {
    return PointerConverter<T, ElementConverter>(elementConverter);
}

/// @brief 参照先の変換方法とポインタ生成方法を指定して pointer-like 型のjson変換方法を返す。
template <typename T, typename ElementConverter, typename PointerFactory>
constexpr auto getPointerConverter(
    const ElementConverter& elementConverter, PointerFactory pointerFactory) {
    return PointerConverter<T, ElementConverter, PointerFactory>(
        elementConverter, std::move(pointerFactory));
}

// ******************************************************************************** トークン種別毎の分岐用

/// @brief トークン種別ごとの読み取り／書き出しを提供する基底的な変換方法。
/// @tparam ValueType 値の型
template <typename ValueType>
struct TokenConverter {
    using Value = ValueType;

    // 読み取り（各トークン種別ごとにオーバーライド可能）
    Value readNull(JsonParser& parser) const {
        if constexpr (std::is_constructible_v<Value, std::nullptr_t>) {
            parser.skipValue();
            return Value(nullptr);
        }
        else {
            throw std::runtime_error("Null is not supported for TokenConverter");
        }
    }

    Value readBool(JsonParser& parser) const {
        return this->template read<bool>(parser, "Bool is not supported for TokenConverter");
    }

    Value readInteger(JsonParser& parser) const {
        return this->template read<int>(parser, "Integer is not supported for TokenConverter");
    }

    Value readNumber(JsonParser& parser) const {
        return this->template read<double>(parser, "Number is not supported for TokenConverter");
    }

    Value readString(JsonParser& parser) const {
        return this->template read<std::string>(parser, "String is not supported for TokenConverter");
    }

    Value readStartObject(JsonParser& parser) const {
        if constexpr (HasSerializer<Value> || (HasReadFormat<Value> && HasWriteFormat<Value>)) {
            Value value{};
            getConverter<Value>().read(parser, value);
            return value;
        }
        else {
            throw std::runtime_error("Object is not supported for TokenConverter");
        }
    }

    Value readStartArray(JsonParser& parser) const {
        // デフォルトでは配列はサポートしない（必要なら派生で実装）
        throw std::runtime_error("Array is not supported for TokenConverter");
    }
protected:
    template <typename T>
    static constexpr Value read(JsonParser& parser, const char* errorMessage) {
        if constexpr (std::is_constructible_v<Value, T>) {
            T s;
            parser.readTo(s);
            return Value(s);
        }
        else {
            throw std::runtime_error(errorMessage);
        }
    }

    void write(JsonWriter& writer, const Value& value) const {
        if constexpr (IsDefaultConverterSupported<Value>) {
            getConverter<Value>().write(writer, value);
        }
        else {
            static_assert(false, "TokenConverter::write: unsupported Value type");
        }
    }
};

/// @brief トークン種別に応じた変換方法
template <typename ValueType, typename TokenConv = TokenConverter<ValueType>>
struct TokenDispatchConverter {
    using Value = ValueType;
    using TokenConvT = std::remove_cvref_t<TokenConv>;
    static_assert(std::is_base_of_v<TokenConverter<Value>, TokenConvT>,
        "TokenConv must be TokenConverter<Value> or derived from it");

    // コンストラクタ（TokenConverter を受け取る）
    constexpr explicit TokenDispatchConverter(const TokenConvT& conv = TokenConvT())
        : tokenConverter_(conv) {}

    /// @brief トークン種別に応じて適切な変換関数を呼び出して値を読み取る。
    std::size_t read(JsonParser& parser, ValueType& out) const {
        switch (parser.nextTokenType()) {
        case JsonTokenType::Null:        out = tokenConverter_.readNull(parser); break;
        case JsonTokenType::Bool:        out = tokenConverter_.readBool(parser); break;
        case JsonTokenType::Integer:     out = tokenConverter_.readInteger(parser); break;
        case JsonTokenType::Number:      out = tokenConverter_.readNumber(parser); break;
        case JsonTokenType::String:      out = tokenConverter_.readString(parser); break;
        case JsonTokenType::StartObject: out = tokenConverter_.readStartObject(parser); break;
        case JsonTokenType::StartArray:  out = tokenConverter_.readStartArray(parser); break;
        default: throw std::runtime_error("Unsupported token type");
        }
        return parser.nextPosition();
    }

    /// @brief 値を JSON に書き出すための関数を呼び出す。
    void write(JsonWriter& writer, const ValueType& value) const {
        tokenConverter_.write(writer, value);
    }

private:
    TokenConvT tokenConverter_{};
};

// ******************************************************************************** variant用変換方法

/// @brief std::variant 型かどうかを判定する concept（std::variant 固有の trait を確認）。
/// @tparam T 判定対象の型。
template <typename T>
concept IsStdVariant = requires {
    typename std::variant_size<T>::type;
};

/// @brief std::variant の要素ごとの変換方法。独自型を扱う場合はこれを継承してカスタマイズする。
/// @tparam Variant std::variant 型
template <typename Variant>
struct VariantElementConverter : TokenConverter<Variant> {
    static_assert(IsStdVariant<Variant>,
        "VariantElementConverter requires Variant to be a std::variant");

    /// @brief Null トークンを読み取り、variant を返す。
    /// @param parser 読み取り元の JsonParser
    /// @return 読み取った値
    Variant readNull(JsonParser& parser) const {
        (void)parser;
        if constexpr (canAssignNullptr()) {
            return Variant{ nullptr };
        }
        throw std::runtime_error("Null is not supported in variant");
    }

    /// @brief Bool トークンを読み取り、variant を返す。
    /// @param parser 読み取り元の JsonParser
    /// @return 読み取った値
    Variant readBool(JsonParser& parser) const {
        if constexpr (canAssign<bool>()) {
            bool value{};
            parser.readTo(value);
            return Variant{ value };
        }
        throw std::runtime_error("Bool is not supported in variant");
    }

    /// @brief Integer トークンを読み取り、variant を返す。
    /// @param parser 読み取り元の JsonParser
    /// @return 読み取った値
    Variant readInteger(JsonParser& parser) const {
        if constexpr (canAssign<int>()) {
            int value{};
            parser.readTo(value);
            return Variant{ value };
        }
        throw std::runtime_error("Integer is not supported in variant");
    }

    /// @brief Number トークンを読み取り、variant を返す。
    /// @param parser 読み取り元の JsonParser
    /// @return 読み取った値
    Variant readNumber(JsonParser& parser) const {
        if constexpr (canAssign<double>()) {
            double value{};
            parser.readTo(value);
            return Variant{ value };
        }
        throw std::runtime_error("Number is not supported in variant");
    }

    /// @brief String トークンを読み取り、variant を返す。
    /// @param parser 読み取り元の JsonParser
    /// @return 読み取った値
    Variant readString(JsonParser& parser) const {
        if constexpr (canAssign<std::string>()) {
            std::string value{};
            parser.readTo(value);
            return Variant{ std::move(value) };
        }
        throw std::runtime_error("String is not supported in variant");
    }

    /// @brief StartArray トークンを読み取り、variant を返す。
    /// @param parser 読み取り元の JsonParser
    /// @return 読み取った値
    Variant readStartArray(JsonParser& parser) const {
        (void)parser;
        throw std::runtime_error("Array is not supported in variant");
    }

    /// @brief StartObject トークンを読み取り、variant を返す。
    /// @param parser 読み取り元の JsonParser
    /// @return 読み取った値
    Variant readStartObject(JsonParser& parser) const {
        bool found = false;
        Variant out{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            // Evaluate alternatives in order; stop at the first that matches
            ((void)(!found && ([&]() {
                using Alt = std::remove_cvref_t<typename std::variant_alternative_t<I, Variant>>;
                if constexpr (HasSerializer<Alt> || (HasReadFormat<Alt> && HasWriteFormat<Alt>)) {
                    getConverter<Alt>().read(parser, out.template emplace<Alt>());
                    found = true;
                }
                return 0;
            }())), ...);
        }(std::make_index_sequence<std::variant_size_v<Variant>>{});

        if (!found) {
            throw std::runtime_error("Object is not supported in variant");
        }
        return out;
    }

    /// @brief Variant 値を JSON に書き出す。
    /// @param writer 書き込み先の JsonWriter
    /// @param value 書き込む値
    void write(
        JsonWriter& writer, const Variant& value) const {
        std::visit([&](const auto& inner) {
            this->write(writer, inner);
        }, value);
    }

    template<typename T>
    void write(JsonWriter& writer, const T& value) const {
        static const auto& conv = getConverter<std::remove_cvref_t<T>>();
        conv.write(writer, value);
    }
private:
    static constexpr bool canAssignNullptr() noexcept {
        using Null = std::nullptr_t;
        return []<size_t... I>(std::index_sequence<I...>) {
            return (std::is_assignable_v<
                typename std::variant_alternative_t<I, Variant>&,
                Null
            > || ...);
        }(std::make_index_sequence<std::variant_size_v<Variant>>{});
    }
private:
    template<class T>
    static constexpr bool canAssign() noexcept {
        using U = std::remove_cvref_t<T>;
        return []<size_t... I>(std::index_sequence<I...>) {
            return (std::is_same_v<U, std::remove_cvref_t<
                typename std::variant_alternative_t<I, Variant>>>
                || ...);
        }(std::make_index_sequence<std::variant_size_v<Variant>>{});
    }
};

/// @brief Variant 用の TokenDispatchConverter を構築するヘルパー（既定の要素変換器）。
template <typename Variant>
constexpr const auto& getVariantConverter() {
    static_assert(IsStdVariant<Variant>,
        "getVariantConverter requires Variant to be a std::variant");
    using ElementConverter = VariantElementConverter<Variant>;
    static TokenDispatchConverter<Variant, ElementConverter> converter(ElementConverter{});
    return converter;
}

/// @brief Variant 用の TokenDispatchConverter を構築するヘルパー（要素変換器指定）。
template <typename Variant, typename ElementConverterType>
constexpr auto getVariantConverter(ElementConverterType elementConverter) {
    static_assert(IsStdVariant<Variant>,
        "getVariantConverter requires Variant to be a std::variant");
    static_assert(std::is_base_of_v<VariantElementConverter<Variant>,
        std::remove_cvref_t<ElementConverterType>>,
        "ElementConverterType must be derived from VariantElementConverter<Variant>");
    using ElementConverter = std::remove_cvref_t<ElementConverterType>;
    return TokenDispatchConverter<Variant, ElementConverter>(
        ElementConverter(std::move(elementConverter)));
}

}  // namespace rai::serialization
