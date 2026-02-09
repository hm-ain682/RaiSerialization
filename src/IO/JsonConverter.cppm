/// @file JsonConverter.cppm
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

export module rai.json.json_converter;

import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_token_manager;

import rai.collection.sorted_hash_array_map;

export namespace rai::json {

// ******************************************************************************** concept

/// @brief JSONへの書き出しと読み込みを行うコンバータに要求される条件を定義する concept。
/// @tparam Converter コンバータ型
/// @tparam Value コンバータが扱う値の型
template <typename Converter, typename Value>
concept IsJsonConverter = std::is_class_v<Converter>
    && requires { typename Converter::Value; }
    && std::is_same_v<typename Converter::Value, Value>
    && requires(const Converter& converter, JsonWriter& writer, const Value& value) {
        converter.write(writer, value);
    }
    && requires(const Converter& converter, JsonParser& parser) {
        { converter.read(parser) } -> std::same_as<Value>;
    };

// ******************************************************************************** 基本型用変換方法

/// @brief プリミティブ型（int, double, bool など）かどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept IsFundamentalValue = std::is_fundamental_v<T>;

/// @brief プリミティブ型、文字列型の変換方法。
template <typename T>
struct FundamentalConverter {
    static_assert(IsFundamentalValue<T> || std::same_as<T, std::string>,
        "FundamentalConverter requires T to be a fundamental JSON value or std::string");
    using Value = T;
    void write(JsonWriter& writer, const T& value) const { writer.writeObject(value); }
    T read(JsonParser& parser) const {
        T out{};
        parser.readTo(out);
        return out;
    }
};

/// @brief jsonFields()メンバー関数を持つかどうかを判定するconcept。
/// @tparam T 判定対象の型。
template <typename T>
concept HasJsonFields = requires(const T& t) { t.jsonFields(); };

/// @brief jsonFields を持つ型のコンバータ
template <typename T>
struct JsonFieldsConverter {
    static_assert(HasJsonFields<T> && std::default_initializable<T>,
        "JsonFieldsConverter requires T to have jsonFields() and be default-initializable");
    using Value = T;
    void write(JsonWriter& writer, const T& obj) const {
        auto& fields = obj.jsonFields();
        writer.startObject();
        fields.writeFields(writer, static_cast<const void*>(&obj));
        writer.endObject();
    }
    T read(JsonParser& parser) const {
        T obj{};
        auto& fields = obj.jsonFields();
        parser.startObject();
        fields.readFields(parser, &obj);
        parser.endObject();
        return obj;
    }
};

/// @brief readJsonメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasReadJson = requires(T& obj, JsonParser& parser) {
    { obj.readJson(parser) } -> std::same_as<void>;
};

/// @brief writeJsonメソッドを持つ型を表すconcept。
/// @tparam T 型。
template <typename T>
concept HasWriteJson = requires(const T& obj, JsonWriter& writer) {
    { obj.writeJson(writer) } -> std::same_as<void>;
};

/// @brief writeJson/readJson を持つ型のコンバータ
template <typename T>
struct ReadWriteJsonConverter {
    static_assert(HasReadJson<T> && HasWriteJson<T> && std::default_initializable<T>,
        "ReadWriteJsonConverter requires T to have readJson/writeJson and be default-initializable");
    using Value = T;
    void write(JsonWriter& writer, const T& obj) const {
        obj.writeJson(writer);
    }
    T read(JsonParser& parser) const {
        T out{};
        out.readJson(parser);
        return out;
    }
};

/// @brief 標準でサポートする型を判定する。
/// @tparam T 判定対象の型
template <typename T>
concept IsDefaultConverterSupported
    = IsFundamentalValue<T>
    || std::same_as<T, std::string>
    || HasJsonFields<T>
    || (HasReadJson<T> && HasWriteJson<T>);

/// @brief 型 `T` に応じた既定のコンバータを返すユーティリティ。
/// @note 基本型、`HasJsonFields`、`HasReadJson`/`HasWriteJson` を持つ型を自動的に扱い、その他の複雑な型は明確な static_assert で除外します。
template <typename T>
constexpr auto& getConverter() {
    if constexpr (IsFundamentalValue<T> || std::same_as<T, std::string>) {
        static const FundamentalConverter<T> inst{};
        return inst;
    }
    else if constexpr (HasJsonFields<T>) {
        static const JsonFieldsConverter<T> inst{};
        return inst;
    }
    else if constexpr (HasReadJson<T> && HasWriteJson<T>) {
        static const ReadWriteJsonConverter<T> inst{};
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

    Enum read(JsonParser& parser) const {
        std::string jsonValue;
        parser.readTo(jsonValue);
        if (auto v = map_.fromName(jsonValue)) {
            return *v;
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

// ******************************************************************************** コンテナ用変換方法

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

/// @brief コンテナの変換方法。
template <typename Container, typename ElementConverter>
struct ContainerConverter {
    static_assert(IsContainer<Container>,
        "ContainerConverter requires Container to be a container type");
    using Value = Container;
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    static_assert(IsJsonConverter<ElementConverter, Element>,
        "ElementConverter must satisfy IsJsonConverter for container element type");
    using ElementConverterT = std::remove_cvref_t<ElementConverter>;
    static_assert(std::is_same_v<typename ElementConverterT::Value, Element>,
        "ElementConverter::Value must match container element type");

    constexpr explicit ContainerConverter(const ElementConverter& elemConv)
        : elementConverter_(std::cref(elemConv)) {}

    void write(JsonWriter& writer, const Container& range) const {
        writer.startArray();
        for (const auto& e : range) {
            elementConverter_.get().write(writer, e);
        }
        writer.endArray();
    }

    Container read(JsonParser& parser) const {
        Container out{};
        parser.startArray();
        while (!parser.nextIsEndArray()) {
            auto elem = elementConverter_.get().read(parser);
            if constexpr (requires(Container& c, Element&& v) {
                    c.push_back(std::declval<Element>());
                }) {
                out.push_back(std::move(elem));
            }
            else if constexpr (requires(Container& c, Element&& v) {
                    c.insert(std::declval<Element>());
                }) {
                out.insert(std::move(elem));
            }
            else {
                static_assert(false,
                    "ContainerConverter: container must support push_back or insert");
            }
        }
        parser.endArray();
        return out;
    }

private:
    std::reference_wrapper<const ElementConverterT> elementConverter_{};
};

/// @brief コンテナ型に対応する既定の `ContainerConverter` を作成する。
/// @tparam Container コンテナ型
template <typename Container>
constexpr auto getContainerConverter() {
    using Elem = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    const auto& elementConverter = getConverter<Elem>();
    using ElemConv = std::remove_cvref_t<decltype(elementConverter)>;
    return ContainerConverter<Container, ElemConv>(elementConverter);
}

/// @brief 明示的な要素コンバータから `ContainerConverter` を作成する。
/// @tparam Container コンテナ型
/// @tparam ElementConverter 要素コンバータ型
/// @param elemConv 要素コンバータ
template <typename Container, typename ElementConverter>
constexpr auto getContainerConverter(const ElementConverter& elemConv) {
    return ContainerConverter<Container, ElementConverter>(elemConv);
}

// ******************************************************************************** unique_ptr用変換方法

/// @brief std::unique_ptr を判定する concept（element_type / deleter_type を確認し正確に判定）。
/// @tparam T 判定対象の型。
template <typename T>
concept IsUniquePtr = requires {
    typename T::element_type;
    typename T::deleter_type;
} && std::is_same_v<T, std::unique_ptr<typename T::element_type, typename T::deleter_type>>;

/// @brief unique_ptr 等のコンバータ
template <typename T, typename TargetConverter>
struct UniquePtrConverter {
    using Value = T;
    using Element = typename T::element_type;
    using ElemConvT = std::remove_cvref_t<TargetConverter>;
    static_assert(IsUniquePtr<T>, "UniquePtrConverter requires T to be a unique_ptr-like type");
    static_assert(IsJsonConverter<ElemConvT, Element>,
        "UniquePtrConverter requires ElementConverter to be a JsonConverter for element type");

    // デフォルト要素コンバータへの参照を返すユーティリティ（静的寿命）
    static const ElemConvT& defaultTargetConverter() {
        static const ElemConvT& inst = getConverter<Element>();
        return inst;
    }

    // デフォルトコンストラクタはデフォルト要素コンバータへの参照を初期化子リストで設定する
    UniquePtrConverter()
        : targetConverter_(std::cref(defaultTargetConverter())) {}

    // 明示的に要素コンバータ参照を指定するオーバーロード
    constexpr explicit UniquePtrConverter(const ElemConvT& conv)
        : targetConverter_(std::cref(conv)) {}

    void write(JsonWriter& writer, const T& ptr) const {
        if (!ptr) {
            writer.null();
            return;
        }
        targetConverter_.get().write(writer, *ptr);
    }

    T read(JsonParser& parser) const {
        if (parser.nextIsNull()) {
            parser.skipValue();
            return nullptr;
        }
        auto elem = targetConverter_.get().read(parser);
        return std::make_unique<Element>(std::move(elem));
    }

private:
    std::reference_wrapper<const ElemConvT> targetConverter_;
};

/// @brief unique_ptr<T>のjson変換方法を返す。※インスタンスはstatic。
/// @tparam T 参照先がgetConverterの対象であるunique_ptr型
template <typename T>
constexpr auto& getUniquePtrConverter() {
    using TargetConverter = decltype(getConverter<typename T::element_type>());
    static const UniquePtrConverter<T, TargetConverter> inst{};
    return inst;
}

/// @brief 参照先の変換方法を指定して unique_ptr<T> のjson変換方法を返す。
/// @tparam T unique_ptr型
/// @param elementConverter 参照先の変換方法
template <typename T, typename ElementConverter>
constexpr auto getUniquePtrConverter(const ElementConverter& elementConverter) {
    return UniquePtrConverter<T, ElementConverter>(elementConverter);
}

// ******************************************************************************** トークン種別毎の分岐用

/// @brief トークン種別ごとの読み取り／書き出しを提供する基底的なコンバータ
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
        if constexpr (HasJsonFields<Value> || (HasReadJson<Value> && HasWriteJson<Value>)) {
            return getConverter<Value>().read(parser);
        }
        else {
            throw std::runtime_error("Object is not supported for TokenConverter");
        }
    }

    Value readStartArray(JsonParser& parser) const {
        // デフォルトでは配列はサポートしない（必要なら派生で実装）
        throw std::runtime_error("Array is not supported for TokenConverter");
    }
private:
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

/// @brief トークン種別に応じた分岐コンバータ（JsonTokenDispatchField と同等）
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
    ValueType read(JsonParser& parser) const {
        switch (parser.nextTokenType()) {
        case JsonTokenType::Null:        return tokenConverter_.readNull(parser);
        case JsonTokenType::Bool:        return tokenConverter_.readBool(parser);
        case JsonTokenType::Integer:     return tokenConverter_.readInteger(parser);
        case JsonTokenType::Number:      return tokenConverter_.readNumber(parser);
        case JsonTokenType::String:      return tokenConverter_.readString(parser);
        case JsonTokenType::StartObject: return tokenConverter_.readStartObject(parser);
        case JsonTokenType::StartArray:  return tokenConverter_.readStartArray(parser);
        default: throw std::runtime_error("Unsupported token type");
        }
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
                if constexpr (HasJsonFields<Alt> || (HasReadJson<Alt> && HasWriteJson<Alt>)) {
                    out = getConverter<Alt>().read(parser);
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
    void write(JsonWriter& writer, const Variant& value) const {
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
constexpr auto getVariantConverter() {
    static_assert(IsStdVariant<Variant>,
        "getVariantConverter requires Variant to be a std::variant");
    using ElementConverter = VariantElementConverter<Variant>;
    return TokenDispatchConverter<Variant, ElementConverter>(ElementConverter{});
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

}  // namespace rai::json
