import rai.serialization.core;
import rai.serialization.json;
import rai.serialization.json_io;
import rai.serialization.test_helper;
import rai.serialization.token_manager;
import rai.collection.sorted_hash_array_map;
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <array>
#include <optional>
#include <set>
#include <utility>
#include <type_traits>
#include <map>
#include <memory>
#include <vector>

using namespace rai::serialization;
using namespace rai::serialization::test;

/// @brief テスト用の構造体A。
struct A {
    bool w = true;
    int x = 1;

    virtual ~A() = default;

    /// @brief JSONフィールドを取得する仮想関数。
    /// @return フィールドプランへの参照。
    /// @note 戻り値はObjectSerializer&で、派生クラスでオーバーライド可能。
    ///       getFieldSetを使用することで型名を簡潔に記述。
    virtual const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&A::w, "w"),
            getRequiredField(&A::x, "x")
        );
        return fields;
    }
};

/// @brief テスト用の構造体B。Aを継承。
struct B : public A {
    float y = 2.0f;

    /// @brief JSONフィールドを取得する仮想関数のオーバーライド。
    /// @return フィールドプランへの参照。
    /// @note A::wとB::yのみを公開（A::xは含まない）。
    ///       getFieldSetを使用することで型名を簡潔に記述。
    const ObjectSerializer& serializer() const override {
        static const auto fields = getFieldSet(
            getRequiredField(&A::w, "w"),
            getRequiredField(&B::y, "y")
        );
        return fields;
    }
};

/// @brief テスト用の構造体C。Aを継承。
struct C : public A {
    std::string z = "hello";

    /// @brief JSONフィールドを取得する仮想関数のオーバーライド。
    /// @return フィールドプランへの参照。
    /// @note A::wとC::zのみを公開（A::xは含まない）。
    ///       getFieldSetを使用することで型名を簡潔に記述。
    const ObjectSerializer& serializer() const override {
        static const auto fields = getFieldSet(
            getRequiredField(&A::w, "w"),
            getRequiredField(&C::z, "z")
        );
        return fields;
    }
};

// ********************************************************************************
// カスタム判別キーを使ったポリモーフィックなフィールド/配列のテスト
// ********************************************************************************

struct PB {
    virtual ~PB() = default;

    virtual const ObjectSerializer& serializer() const {
        static const auto f = FieldsObjectSerializer<PB>{};
        return f;
    }

    /// @brief ポリモーフィックな比較演算子。
    /// @param other 比較対象のオブジェクト。
    /// @return 等しい場合はtrue、そうでない場合はfalse。
    virtual bool operator==(const PB& other) const = 0;
};

struct POne : public PB {
    int x = 0;

    const ObjectSerializer& serializer() const override {
        static const auto f = getFieldSet(
            getRequiredField(&POne::x, "x")
        );
        return f;
    }

    bool operator==(const PB& other) const override {
        auto* p = dynamic_cast<const POne*>(&other);
        return p != nullptr && x == p->x;
    }
};

struct PTwo : public PB {
    std::string s;

    const ObjectSerializer& serializer() const override {
        static const auto f = getFieldSet(
            getRequiredField(&PTwo::s, "s")
        );
        return f;
    }

    bool operator==(const PB& other) const override {
        auto* p = dynamic_cast<const PTwo*>(&other);
        return p != nullptr && s == p->s;
    }
};

struct PolymorphicFactorySeed {
    int value = 0;
};

struct PFromFactoryContext : public PB {
    int seeded = 0;

    const ObjectSerializer& serializer() const override {
        static const auto f = FieldsObjectSerializer<PFromFactoryContext>{};
        return f;
    }

    bool operator==(const PB& other) const override {
        auto* p = dynamic_cast<const PFromFactoryContext*>(&other);
        return p != nullptr && seeded == p->seeded;
    }
};

// entries を直接マップ構築（配列を経由せず簡潔に記述）
inline const auto pbEntriesMap = std::array{
    makePolymorphicTypeEntry<POne>("One",
        [](JsonParser&) -> std::unique_ptr<PB> { return std::make_unique<POne>(); }),
    makePolymorphicTypeEntry<PTwo>("Two",
        [](JsonParser&) -> std::unique_ptr<PB> { return std::make_unique<PTwo>(); })
};

inline const auto contextFactoryEntriesMap = std::array{
    makePolymorphicTypeEntry<PFromFactoryContext>("FromContext",
    [](JsonParser& parser) -> std::unique_ptr<PB> {
        auto instance = std::make_unique<PFromFactoryContext>();
        instance->seeded = parser.context<PolymorphicFactorySeed>()->value;
        return instance;
    })
};

struct Holder {
    std::unique_ptr<PB> item;
    std::vector<std::unique_ptr<PB>> arr;

    const ObjectSerializer& serializer() const {
        static const auto itemConverter = getPolymorphicConverter<decltype(item)>(
            pbEntriesMap, "kind");
        static const auto arrayConverter =
            getPolymorphicArrayConverter<decltype(arr)>(pbEntriesMap, "kind");
        static const auto fields = getFieldSet(
            getRequiredField(&Holder::item, "item", itemConverter),
            getRequiredField(&Holder::arr, "arr", arrayConverter)
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const Holder& other) const {
        bool itemMatch = (item == nullptr && other.item == nullptr) ||
            (item != nullptr && other.item != nullptr && *item == *other.item);
        if (!itemMatch) {
            return false;
        }

        if (arr.size() != other.arr.size()) {
            return false;
        }
        for (size_t i = 0; i < arr.size(); ++i) {
            bool elemMatch = (arr[i] == nullptr && other.arr[i] == nullptr) ||
                (arr[i] != nullptr && other.arr[i] != nullptr && *arr[i] == *other.arr[i]);
            if (!elemMatch) {
                return false;
            }
        }
        return true;
    }
};

struct ContextFactoryHolder {
    std::unique_ptr<PB> item;

    const ObjectSerializer& serializer() const {
        static const auto itemConverter = getPolymorphicConverter<decltype(item)>(
            contextFactoryEntriesMap, "kind");
        static const auto fields = getFieldSet(
            getRequiredField(&ContextFactoryHolder::item, "item", itemConverter)
        );
        return fields;
    }
};

template <typename T>
class NonStdOwningPtr {
public:
    using element_type = T;

    NonStdOwningPtr(std::nullptr_t = nullptr) {}
    explicit NonStdOwningPtr(T* ptr)
        : ptr_(ptr) {}

    NonStdOwningPtr(NonStdOwningPtr&&) noexcept = default;
    NonStdOwningPtr& operator=(NonStdOwningPtr&&) noexcept = default;
    NonStdOwningPtr(const NonStdOwningPtr&) = delete;
    NonStdOwningPtr& operator=(const NonStdOwningPtr&) = delete;

    T& operator*() {
        return *ptr_;
    }

    T& operator*() const {
        return *ptr_;
    }

    T* get() const {
        return ptr_.get();
    }

    explicit operator bool() const {
        return static_cast<bool>(ptr_);
    }

    friend bool operator==(const NonStdOwningPtr& ptr, std::nullptr_t) {
        return ptr.ptr_ == nullptr;
    }

    friend bool operator==(std::nullptr_t, const NonStdOwningPtr& ptr) {
        return ptr == nullptr;
    }

    friend bool operator!=(const NonStdOwningPtr& ptr, std::nullptr_t) {
        return !(ptr == nullptr);
    }

    friend bool operator!=(std::nullptr_t, const NonStdOwningPtr& ptr) {
        return !(ptr == nullptr);
    }

private:
    std::unique_ptr<T> ptr_;
};

using NonStdPBPtr = NonStdOwningPtr<PB>;
inline const auto nonStdPbEntriesMap = std::array{
    makePolymorphicTypeEntry<POne>("One",
        [](JsonParser&) { return NonStdPBPtr(new POne()); }),
    makePolymorphicTypeEntry<PTwo>("Two",
        [](JsonParser&) { return NonStdPBPtr(new PTwo()); })
};

struct NonStdPtrHolder {
    NonStdPBPtr item;

    const ObjectSerializer& serializer() const {
        static const auto itemConverter = getPolymorphicConverter<decltype(item)>(
            nonStdPbEntriesMap, "kind");
        static const auto fields = getFieldSet(
            getRequiredField(&NonStdPtrHolder::item, "item", itemConverter)
        );
        return fields;
    }

    bool equals(const NonStdPtrHolder& other) const {
        if (item == nullptr || other.item == nullptr) {
            return item == nullptr && other.item == nullptr;
        }
        return *item == *other.item;
    }
};

// ********************************************************************************
// FieldSerializer の既定値と書き出し省略に関するテスト
// ********************************************************************************

struct ExternalPB {
    virtual ~ExternalPB() = default;
    virtual bool operator==(const ExternalPB& other) const = 0;
};

struct ExternalOne : public ExternalPB {
    int x = 0;

    bool operator==(const ExternalPB& other) const override {
        auto* p = dynamic_cast<const ExternalOne*>(&other);
        return p != nullptr && x == p->x;
    }
};

struct ExternalTwo : public ExternalPB {
    std::string s;

    bool operator==(const ExternalPB& other) const override {
        auto* p = dynamic_cast<const ExternalTwo*>(&other);
        return p != nullptr && s == p->s;
    }
};

inline const auto externalOneFields = getFieldSet(
    getRequiredField(&ExternalOne::x, "x")
);

inline const auto externalTwoFields = getFieldSet(
    getRequiredField(&ExternalTwo::s, "s")
);

using ExternalPtr = std::unique_ptr<ExternalPB>;
inline const auto externalEntriesMap = std::array{
    makePolymorphicSerializerEntry<ExternalOne>("One",
        [](JsonParser&) -> ExternalPtr { return std::make_unique<ExternalOne>(); },
        externalOneFields),
    makePolymorphicSerializerEntry<ExternalTwo>("Two",
        [](JsonParser&) -> ExternalPtr { return std::make_unique<ExternalTwo>(); },
        externalTwoFields)
};

struct ExternalHolder {
    ExternalPtr item;
    std::vector<ExternalPtr> arr;

    const ObjectSerializer& serializer() const {
        static const auto itemConverter =
            getPolymorphicConverter<decltype(item)>(externalEntriesMap, "kind");
        static const auto arrayConverter =
            getPolymorphicArrayConverter<decltype(arr)>(externalEntriesMap, "kind");
        static const auto fields = getFieldSet(
            getRequiredField(&ExternalHolder::item, "item", itemConverter),
            getRequiredField(&ExternalHolder::arr, "arr", arrayConverter)
        );
        return fields;
    }

    bool equals(const ExternalHolder& other) const {
        bool itemMatch = (item == nullptr && other.item == nullptr) ||
            (item != nullptr && other.item != nullptr && *item == *other.item);
        if (!itemMatch || arr.size() != other.arr.size()) {
            return false;
        }
        for (size_t i = 0; i < arr.size(); ++i) {
            bool elemMatch = (arr[i] == nullptr && other.arr[i] == nullptr) ||
                (arr[i] != nullptr && other.arr[i] != nullptr && *arr[i] == *other.arr[i]);
            if (!elemMatch) {
                return false;
            }
        }
        return true;
    }
};

struct DefaultFieldTest {
    int a = 0;
    int b = 0;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&DefaultFieldTest::a, "a"),
            getDefaultOmittedField(&DefaultFieldTest::b, "b", 42)
        );
        return fields;
    }
};

struct SkipFieldTest {
    int a = 1;  ///< 必須値。
    int b = 0;  ///< 省略判定対象。

    /// @brief JSONフィールド定義を返す。
    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&SkipFieldTest::a, "a"),
            getDefaultOmittedField(&SkipFieldTest::b, "b", 0)
        );
        return fields;
    }
};

struct InitialOmitFieldTest {
    int a = 1;
    int b = 7;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&InitialOmitFieldTest::a, "a"),
            getInitialOmittedField(&InitialOmitFieldTest::b, "b", 0)
        );
        return fields;
    }
};

struct PropertyFieldTest {
private:
    std::string bar;

public:
    const std::string& getBar() const {
        return bar;
    }

    void setBar(std::string_view value) {
        bar = value;
    }

    bool equals(const PropertyFieldTest& other) const {
        return bar == other.bar;
    }
};

TEST(JsonPropertySerializer, ReadWriteViaGetterSetter) {
    PropertyFieldTest original;
    original.setBar("hello");

    static const auto fields = getFieldSet(
        getRequiredProperty(&PropertyFieldTest::getBar, &PropertyFieldTest::setBar, "bar")
    );
    testJsonRoundTrip(original, "{bar:\"hello\"}",
        getObjectSerializerConverter<PropertyFieldTest>(fields));
}

TEST(JsonPolymorphicTest, ReadSingleCustomKey) {
    Holder original;
    original.item = std::make_unique<POne>();
    dynamic_cast<POne*>(original.item.get())->x = 42;
    testJsonRoundTrip(original, "{item:{kind:\"One\",x:42},arr:[]}");
}

TEST(JsonPolymorphicTest, ReadArrayCustomKeyAndNull) {
    Holder original;
    auto one = std::make_unique<POne>();
    one->x = 1;
    original.arr.push_back(std::move(one));

    auto two = std::make_unique<PTwo>();
    two->s = "abc";
    original.arr.push_back(std::move(two));
    original.arr.push_back(nullptr);

    testJsonRoundTrip(original,
        "{item:null,arr:[{kind:\"One\",x:1},{kind:\"Two\",s:\"abc\"},null]}");
}

TEST(JsonPolymorphicTest, WriteAndReadRoundTripUsingCustomKey) {
    auto one = std::make_unique<POne>();
    one->x = 99;
    Holder original;
    original.item = std::move(one);
    testJsonRoundTrip(original, "{item:{kind:\"One\",x:99},arr:[]}");
}

TEST(JsonPolymorphicTest, WriteUsesTypeNameMapWithoutCallingFactory) {
    ContextFactoryHolder original;
    original.item = std::make_unique<PFromFactoryContext>();
    dynamic_cast<PFromFactoryContext*>(original.item.get())->seeded = 123;
    EXPECT_EQ(getJsonContent(original), "{item:{kind:\"FromContext\"}}");
}

TEST(JsonPolymorphicTest, NonStdPointerLikeRoundTrip) {
    NonStdPtrHolder original;
    original.item = NonStdPBPtr(new POne());
    dynamic_cast<POne*>(&*original.item)->x = 123;

    testJsonRoundTrip(original, "{item:{kind:\"One\",x:123}}");
}

TEST(JsonPolymorphicTest, ExternalSerializerEntryRoundTrip) {
    auto one = std::make_unique<ExternalOne>();
    one->x = 7;
    ExternalHolder original;
    original.item = std::move(one);

    auto two = std::make_unique<ExternalTwo>();
    two->s = "abc";
    original.arr.push_back(std::move(two));
    original.arr.push_back(nullptr);

    testJsonRoundTrip(original,
        "{item:{kind:\"One\",x:7},arr:[{kind:\"Two\",s:\"abc\"},null]}");
}

TEST(JsonFieldDefaults, MissingKeySetsDefault) {
    DefaultFieldTest obj{};
    readJsonString("{a:1}", obj);
    EXPECT_EQ(obj.a, 1);
    EXPECT_EQ(obj.b, 42);

    // キーが存在するときは既定値は適用されない
    readJsonString("{a:2,b:7}", obj);
    EXPECT_EQ(obj.a, 2);
    EXPECT_EQ(obj.b, 7);
}

TEST(JsonFieldSkipWrite, OmitWhenValueMatches) {
    SkipFieldTest s{};
    s.a = 1;
    s.b = 0;
    EXPECT_EQ(getJsonContent(s), "{a:1}");

    s.b = 5;
    EXPECT_EQ(getJsonContent(s), "{a:1,b:5}");
}

TEST(JsonInitialFieldDefaults, MissingKeyKeepsInitialValue) {
    InitialOmitFieldTest obj{};
    obj.b = 7;
    readJsonString("{a:10}", obj);
    EXPECT_EQ(obj.a, 10);
    EXPECT_EQ(obj.b, 7);
}

TEST(JsonInitialFieldSkipWrite, OmitWhenValueMatchesDefault) {
    InitialOmitFieldTest obj{};
    obj.a = 1;
    obj.b = 0;
    EXPECT_EQ(getJsonContent(obj), "{a:1}");

    obj.b = 5;
    EXPECT_EQ(getJsonContent(obj), "{a:1,b:5}");
}

// ********************************************************************************
// テストカテゴリ：整数型
// ********************************************************************************

/// @brief 整数型を含む構造体。
struct IntegerTypes {
    short s = 0;
    unsigned short us = 0;
    int i = 0;
    unsigned int ui = 0;
    long l = 0;
    unsigned long ul = 0;
    long long ll = 0;
    unsigned long long ull = 0;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&IntegerTypes::s, "s"),
            getRequiredField(&IntegerTypes::us, "us"),
            getRequiredField(&IntegerTypes::i, "i"),
            getRequiredField(&IntegerTypes::ui, "ui"),
            getRequiredField(&IntegerTypes::l, "l"),
            getRequiredField(&IntegerTypes::ul, "ul"),
            getRequiredField(&IntegerTypes::ll, "ll"),
            getRequiredField(&IntegerTypes::ull, "ull")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const IntegerTypes& other) const {
        return s == other.s && us == other.us && i == other.i && ui == other.ui &&
               l == other.l && ul == other.ul && ll == other.ll && ull == other.ull;
    }
};

/// @brief 整数型の読み書きテスト。
TEST(JsonIntegerTest, ReadWriteRoundTrip) {
    IntegerTypes original;
    original.s = -1000;
    original.us = 2000;
    original.i = -3000000;
    original.ui = 4000000;
    original.l = -2000000000L;
    original.ul = 3000000000UL;
    original.ll = 1234567890123456LL;
    original.ull = 9876543210987654ULL;
    testJsonRoundTrip(original, "{s:-1000,"
        "us:2000,"
        "i:-3000000,"
        "ui:4000000,"
        "l:-2000000000,"
        "ul:3000000000,"
        "ll:1234567890123456,"
        "ull:9876543210987654}");
}

// ********************************************************************************
// テストカテゴリ：浮動小数点数型
// ********************************************************************************

/// @brief 浮動小数点数型を含む構造体。
struct FloatingPointTypes {
    float f = 0.0f;
    double d = 0.0;
    long double ld = 0.0L;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&FloatingPointTypes::f, "f"),
            getRequiredField(&FloatingPointTypes::d, "d"),
            getRequiredField(&FloatingPointTypes::ld, "ld")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const FloatingPointTypes& other) const {
        return f == other.f && d == other.d && ld == other.ld;
    }
};

/// @brief 浮動小数点数型の読み書きテスト。
TEST(JsonFloatingPointTest, ReadWriteRoundTrip) {
    FloatingPointTypes original;
    original.f = 1.5f;
    original.d = -2.75;
    original.ld = 3.125L;

    testJsonRoundTrip(original, "{f:1.5,d:-2.75,ld:3.125}");
}

// ********************************************************************************
// テストカテゴリ：文字型
// ********************************************************************************

/// @brief 文字型を含む構造体。
struct CharacterTypes {
    char c = 'X';
    signed char sc = 'Y';
    unsigned char uc = 'Z';
    char8_t c8 = u8'a';
    char16_t c16 = u'\u30A2';
    char32_t c32 = U'\U0001F389';
    wchar_t wc = L'\u30A6';

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&CharacterTypes::c, "c"),
            getRequiredField(&CharacterTypes::sc, "sc"),
            getRequiredField(&CharacterTypes::uc, "uc"),
            getRequiredField(&CharacterTypes::c8, "c8"),
            getRequiredField(&CharacterTypes::c16, "c16"),
            getRequiredField(&CharacterTypes::c32, "c32"),
            getRequiredField(&CharacterTypes::wc, "wc")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const CharacterTypes& other) const {
        return c == other.c && sc == other.sc && uc == other.uc && c8 == other.c8 &&
               c16 == other.c16 && c32 == other.c32 && wc == other.wc;
    }
};

/// @brief 文字型の読み書きテスト。
TEST(JsonCharacterTest, ReadWriteRoundTrip) {
    CharacterTypes original;
    original.c = 'A';
    original.sc = 'B';
    original.uc = 'C';
    original.c8 = u8'd';
    original.c16 = u'\u30A2';
    original.c32 = U'\u00E9';
    original.wc = L'\u00E8';

    // 注: 文字型は escapeString で出力されるため、Unicode 文字は \uXXXX 形式
    // c16:u'ア' (U+30A2) → \u30a2 (BMP範囲のみ対応。補助平面はサロゲートペアが必要だがchar16_tではサポートされない)
    // c32:U'é' (U+00E9) → \u00e9 (char32_tで完全サポート)
    // wc:L'è' (U+00E8) → \u00e8
    testJsonRoundTrip(original, "{c:\"A\",sc:\"B\",uc:\"C\"," \
        "c8:\"d\",c16:\"\\u30a2\",c32:\"\\u00e9\",wc:\"\\u00e8\"}");
}

// サロゲートペア形式の JSON 文字列（\ud83c\udf89 = 🎉, U+1F389）
// これは char16_t では格納できない補助平面の文字
struct TestHolder {
    char16_t c16 = 0;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&TestHolder::c16, "c16")
        );
        return fields;
    }
};

/// @brief char16_t でサロゲートペア形式の JSON 文字列を読み込むテスト。
/// @note char16_t は BMP 範囲のみサポートするため、補助平面（サロゲートペア）の
///       読み込みは失敗することを確認する。
TEST(JsonCharacterTest, ReadChar16WithSurrogatePair) {
    std::string jsonWithSurrogatePair = R"({c16:"\ud83c\udf89"})";

    TestHolder holder;
    // サロゲートペアは char16_t では格納できないため、例外が発生することを期待
    try {
        readJsonString(jsonWithSurrogatePair, holder);
        // エラーが発生すると期待しているのでここに到達しない
        FAIL() << "Expected exception for surrogate pair in char16_t";
    } catch (const std::exception& e) {
        // エラーメッセージが期待通りであることを確認
        std::string errorMsg(e.what());
        // エラーが発生したことを確認（エラーメッセージは何でもよい）
        EXPECT_FALSE(errorMsg.empty());
    }
}

// ********************************************************************************
// テストカテゴリ：ネストされたオブジェクト
// ********************************************************************************

/// @brief ネストされたオブジェクト構造。
struct NestedChild {
    int value = 0;
    std::string name;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&NestedChild::value, "value"),
            getRequiredField(&NestedChild::name, "name")
        );
        return fields;
    }

    bool operator==(const NestedChild& other) const {
        return value == other.value && name == other.name;
    }
};

/// @brief ネストされたオブジェクトを含む親構造体。
struct NestedParent {
    NestedChild child;
    bool flag = false;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&NestedParent::child, "child"),
            getRequiredField(&NestedParent::flag, "flag")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const NestedParent& other) const {
        return child == other.child && flag == other.flag;
    }
};

/// @brief ネストされたオブジェクトの読み書きテスト。
TEST(JsonNestedTest, ReadWriteRoundTrip) {
    // テスト用に異なる値を設定
    NestedParent original;
    original.child.value = 42;
    original.child.name = "test";
    original.flag = true;

    testJsonRoundTrip(original, "{child:{value:42,name:\"test\"},flag:true}");
}

/// @brief ネスト型のフィールドにオブジェクトシリアライザーを明示的に指定するテスト用の子オブジェクト。
struct ExplicitNestedChild {
    int value = 0;
    std::string name;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&ExplicitNestedChild::value, "value"),
            getRequiredField(&ExplicitNestedChild::name, "name")
        );
        return fields;
    }

    bool operator==(const ExplicitNestedChild& other) const {
        return value == other.value && name == other.name;
    }
};

/// @brief ネスト型のフィールドに明示的なオブジェクトシリアライザーを指定する親オブジェクト。
struct ExplicitNestedParent {
    ExplicitNestedChild child;
    bool flag = false;

    const ObjectSerializer& serializer() const {
        static const auto childFields = getFieldSet(
            getRequiredField(&ExplicitNestedChild::value, "v"),
            getRequiredField(&ExplicitNestedChild::name, "n")
        );
        static const auto childConverter =
            getObjectSerializerConverter<ExplicitNestedChild>(childFields);
        static const auto fields = getFieldSet(
            getRequiredField(&ExplicitNestedParent::child, "child", childConverter),
            getRequiredField(&ExplicitNestedParent::flag, "flag")
        );
        return fields;
    }

    bool equals(const ExplicitNestedParent& other) const {
        return child == other.child && flag == other.flag;
    }
};

TEST(JsonNestedConverterTest, WriteUsesExplicitConverterForNestedObject) {
    ExplicitNestedParent original;
    original.child.value = 7;
    original.child.name = "child";
    original.flag = true;

    testJsonRoundTrip(original, "{child:{v:7,n:\"child\"},flag:true}");
}

TEST(JsonNestedConverterTest, ReadUsesExplicitConverterForNestedObject) {
    ExplicitNestedParent out;

    readJsonString("{child:{v:15,n:\"nested\"},flag:false}", out);

    EXPECT_EQ(out.child.value, 15);
    EXPECT_EQ(out.child.name, "nested");
    EXPECT_FALSE(out.flag);
}

// ********************************************************************************
// テストカテゴリ：optional
// ********************************************************************************

/// @brief optional の要素型 `int` を文字列として扱うコンバータ。
struct IntegerStringConverter {
    using Value = int;

    /// @brief 整数値を文字列として JSON に書き出す。
    /// @param writer 書き込み先のWriter。
    /// @param value 書き出す整数値。
    void write(FormatWriter& writer, const int& value) const {
        writer.writeObject(std::to_string(value));
    }

    /// @brief JSON 文字列から整数値を読み込む。
    /// @param parser 読み込み元のParser。
    /// @return 読み込んだ整数値。
    std::size_t read(FormatReader& parser, int& out) const {
        std::string numberText;
        parser.readTo(numberText);
        out = std::stoi(numberText);
        return parser.nextPosition();
    }
};

/// @brief 既定の optional 変換を検証するための構造体。
struct OptionalHolder {
    std::optional<int> number;               ///< 数値の optional 値。
    std::optional<std::string> text;         ///< 文字列の optional 値。

    /// @brief フィールド定義を返す。
    /// @return フィールド定義。
    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&OptionalHolder::number, "number"),
            getRequiredField(&OptionalHolder::text, "text")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値なら true。
    bool equals(const OptionalHolder& other) const {
        return number == other.number && text == other.text;
    }
};

/// @brief optional 要素コンバータ指定を検証するための構造体。
struct OptionalCustomConverterHolder {
    std::optional<int> numberAsString;   ///< 文字列として入出力する optional 数値。

    /// @brief フィールド定義を返す。
    /// @return フィールド定義。
    const ObjectSerializer& serializer() const {
        static const IntegerStringConverter integerStringConverter{};
        static const auto optionalIntegerConverter =
            getOptionalConverter<decltype(numberAsString)>(integerStringConverter);
        static const auto fields = getFieldSet(
            getRequiredField(
                &OptionalCustomConverterHolder::numberAsString,
                "numberAsString",
                optionalIntegerConverter)
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値なら true。
    bool equals(const OptionalCustomConverterHolder& other) const {
        return numberAsString == other.numberAsString;
    }
};

/// @brief optional の既定コンバータで値ありを読み書きできることを確認する。
TEST(JsonOptionalTest, ReadWriteRoundTripWithValue) {
    OptionalHolder original;
    original.number = 123;
    original.text = "hello";
    testJsonRoundTrip(original, "{number:123,text:\"hello\"}");
}

/// @brief optional の既定コンバータで null を読み書きできることを確認する。
TEST(JsonOptionalTest, ReadWriteRoundTripWithNull) {
    OptionalHolder original;
    testJsonRoundTrip(original, "{number:null,text:null}");
}

/// @brief optional 要素コンバータを指定して変換方法を差し替えられることを確認する。
TEST(JsonOptionalTest, UsesCustomElementConverter) {
    OptionalCustomConverterHolder original;
    original.numberAsString = 42;
    testJsonRoundTrip(original, "{numberAsString:\"42\"}");

    OptionalCustomConverterHolder out;
    readJsonString("{numberAsString:\"105\"}", out);
    ASSERT_TRUE(out.numberAsString.has_value());
    EXPECT_EQ(*out.numberAsString, 105);
}

struct NonMovableValue {
    int value = 0;

    NonMovableValue() = default;
    NonMovableValue(const NonMovableValue&) = delete;
    NonMovableValue& operator=(const NonMovableValue&) = delete;
    NonMovableValue(NonMovableValue&&) = delete;
    NonMovableValue& operator=(NonMovableValue&&) = delete;
};

struct NonMovableValueConverter {
    using Value = NonMovableValue;

    void write(FormatWriter& writer, const Value& value) const {
        writer.writeObject(value.value);
    }

    std::size_t read(FormatReader& parser, Value& out) const {
        parser.readTo(out.value);
        return parser.nextPosition();
    }
};

struct NonMovableHolder {
    NonMovableValue item;

    const ObjectSerializer& serializer() const {
        static const NonMovableValueConverter converter{};
        static const auto fields = getFieldSet(
            getRequiredField(&NonMovableHolder::item, "item", converter)
        );
        return fields;
    }

    bool equals(const NonMovableHolder& other) const {
        return item.value == other.item.value;
    }
};

TEST(JsonObjectConverterTest, ReadsNonMovableMemberThroughConverter) {
    static_assert(!std::is_copy_constructible_v<NonMovableValue>);
    static_assert(!std::is_move_constructible_v<NonMovableValue>);

    NonMovableHolder original;
    original.item.value = 77;
    testJsonRoundTrip(original, "{item:77}");
}

// ********************************************************************************
// テストカテゴリ：FormatContext
// ********************************************************************************

struct ReadScale {
    int factor = 1;
};

struct ContextScaledIntConverter {
    using Value = int;

    void write(FormatWriter& writer, const int& value) const {
        writer.writeObject(value);
    }

    std::size_t read(FormatReader& parser, int& out) const {
        int raw{};
        parser.readTo(raw);
        out = raw * parser.context<ReadScale>()->factor;
        return parser.nextPosition();
    }
};

struct ContextScaledHolder {
    int value = 0;

    const ObjectSerializer& serializer() const {
        static const ContextScaledIntConverter converter{};
        static const auto fields = getFieldSet(
            getRequiredField(&ContextScaledHolder::value, "value", converter)
        );
        return fields;
    }
};

TEST(JsonObjectConverterTest, ConverterCanReadCallerContextFromParser) {
    ReadScale scale{3};
    ContextScaledHolder out;
    readJsonString("{value:14}", out, defaultFormatIssueSink(), scale);
    EXPECT_EQ(out.value, 42);
}

TEST(JsonObjectConverterTest, PolymorphicFactoryCanReadCallerContextFromParser) {
    PolymorphicFactorySeed seed{123};
    ContextFactoryHolder out;
    readJsonString("{item:{kind:\"FromContext\"}}", out, defaultFormatIssueSink(), seed);
    auto* item = dynamic_cast<PFromFactoryContext*>(out.item.get());
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->seeded, 123);
}

// ********************************************************************************
// テストカテゴリ：FormatIssueSink
// ********************************************************************************

struct RecordingFormatIssueSink : FormatIssueSink {
    std::vector<std::string> unknownKeys;
    std::vector<std::size_t> unknownKeyPositions;
    std::vector<std::string> errors;
    bool continueOnError = false;

    void unknownKey(std::string_view key, std::size_t position) override {
        unknownKeys.emplace_back(key);
        unknownKeyPositions.push_back(position);
    }

    bool recoverableError(std::string_view message, std::size_t position) override {
        (void)position;
        errors.emplace_back(message);
        return continueOnError;
    }
};

template <typename T>
void readJsonStringWithIssueSink(const std::string& jsonText, T& out, FormatIssueSink& issueSink) {
    std::string buffer = jsonText;
    constexpr std::size_t aheadSize = 8;
    buffer.reserve(buffer.size() + aheadSize);
    ReadingAheadBuffer inputSource(std::move(buffer), aheadSize);
    TokenManager tokenManager;
    StdoutMessageOutput warningOutput;
    JsonTokenizer<ReadingAheadBuffer, TokenManager> tokenizer(
        inputSource, tokenManager, warningOutput);
    tokenizer.tokenize();

    JsonParser parser(tokenManager, FormatContext{}, issueSink);
    readJsonObject(parser, out);
}

TEST(JsonFormatIssueSinkTest, UnknownKeyCollectionUsesSink) {
    A out;
    RecordingFormatIssueSink issueSink;
    readJsonString("{w:true,x:1,extra:2,nested:{a:1}}", out, issueSink);
    EXPECT_EQ(issueSink.unknownKeys, (std::vector<std::string>{"extra", "nested"}));
}

TEST(JsonFormatIssueSinkTest, CustomSinkReceivesUnknownKeys) {
    A out;
    RecordingFormatIssueSink issueSink;
    const std::string json = "{w:true,x:1,extra:2}";
    readJsonStringWithIssueSink(json, out, issueSink);
    EXPECT_EQ(issueSink.unknownKeys, std::vector<std::string>{"extra"});
    EXPECT_EQ(issueSink.unknownKeyPositions, std::vector<std::size_t>{json.find('2')});
}

TEST(JsonFormatIssueSinkTest, RecoverableValueErrorCanContinue) {
    A out;
    out.x = 99;
    RecordingFormatIssueSink issueSink;
    issueSink.continueOnError = true;
    readJsonStringWithIssueSink("{w:true,x:\"bad\"}", out, issueSink);
    EXPECT_EQ(out.x, 99);
    EXPECT_EQ(issueSink.errors, std::vector<std::string>{"JsonParser: expected integer"});
}

TEST(JsonFormatIssueSinkTest, RecoverableValueErrorSkipsCurrentValue) {
    A out;
    out.x = 99;
    RecordingFormatIssueSink issueSink;
    issueSink.continueOnError = true;
    readJsonStringWithIssueSink("{w:true,x:{nested:[1,2,3]}}", out, issueSink);
    EXPECT_EQ(out.x, 99);
    EXPECT_EQ(issueSink.errors, std::vector<std::string>{"JsonParser: expected integer"});
}

TEST(JsonFormatIssueSinkTest, RecoverableValueErrorThrowsByDefault) {
    A out;
    EXPECT_THROW(readJsonString("{w:true,x:\"bad\"}", out), std::runtime_error);
}

// ********************************************************************************
// テストカテゴリ：トークン種別ディスパッチフィールド
// ********************************************************************************

/// @brief トークン種別ディスパッチフィールド用の値型。
/// @details 文字列、整数、真偽値のいずれかを保持する。
struct DispatchValue {
    std::variant<std::string, int64_t, bool> data;

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool operator==(const DispatchValue& other) const {
        return data == other.data;
    }
};

/// @brief トークン種別ディスパッチフィールドを含む構造体。
struct TokenDispatchHolder {
    DispatchValue value;

    const ObjectSerializer& serializer() const {
        /// @brief テスト用の簡易トークンコンバータ。
        /// 必要最小限のトークンハンドラだけを実装する。
        struct FromConv : TokenConverter<DispatchValue>
        {
            /// @brief Bool トークンを読み取る。
            DispatchValue readBool(JsonParser& p) const
            {
                bool b;
                p.readTo(b);
                return DispatchValue{ b };
            }

            /// @brief Integer トークンを読み取る。
            DispatchValue readInteger(JsonParser& p) const
            {
                int64_t i;
                p.readTo(i);
                return DispatchValue{ i };
            }

            /// @brief String トークンを読み取る。
            DispatchValue readString(JsonParser& p) const
            {
                std::string s;
                p.readTo(s);
                return DispatchValue{ s };
            }

            /// @brief 値を JSON に書き出す。
            /// @param w 書き込み先。
            /// @param v 書き出す値。
            void write(JsonWriter& w, const DispatchValue& v) const {
                std::visit([&w](const auto& val) {
                    w.writeObject(val);
                }, v.data);
            }
        };

        auto tokenConv = FromConv();
        static const TokenDispatchConverter<DispatchValue, FromConv> conv(tokenConv);
        static const auto fields = getFieldSet(
            getRequiredField(&TokenDispatchHolder::value, "value", conv)
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const TokenDispatchHolder& other) const {
        return value == other.value;
    }
};

/// @brief トークンディスパッチフィールドの文字列読み書きテスト。
TEST(JsonTokenDispatchTest, ReadWriteString) {
    TokenDispatchHolder original;
    original.value.data = std::string("hello");
    testJsonRoundTrip(original, "{value:\"hello\"}");
}

/// @brief トークンディスパッチフィールドの整数読み書きテスト。
TEST(JsonTokenDispatchTest, ReadWriteInteger) {
    TokenDispatchHolder original;
    original.value.data = int64_t(42);
    testJsonRoundTrip(original, "{value:42}");
}

/// @brief トークンディスパッチフィールドの真偽値読み書きテスト。
TEST(JsonTokenDispatchTest, ReadWriteBool) {
    TokenDispatchHolder original;
    original.value.data = true;
    testJsonRoundTrip(original, "{value:true}");
}

/// @brief トークンディスパッチフィールドの偽値読み書きテスト。
TEST(JsonTokenDispatchTest, ReadWriteFalse) {
    TokenDispatchHolder original;
    original.value.data = false;
    testJsonRoundTrip(original, "{value:false}");
}

// ********************************************************************************
// HasReadFormat/HasWriteFormat テスト
// ********************************************************************************

/// @brief read/writeメソッドを持つテスト用構造体。
struct CustomFormatType {
    int value = 0;
    std::string name;

    /// @brief 既定フォーマットへの書き出し。
    /// @param writer FormatWriterの参照。
    void write(FormatWriter& writer) const {
        writer.startObject();
        writer.key("value");
        writer.writeObject(value);
        writer.key("name");
        writer.writeObject(name);
        writer.endObject();
    }

    /// @brief 既定フォーマットからの読み込み。
    /// @param parser FormatReaderの参照。
    std::size_t read(FormatReader& parser) {
        parser.startObject();
        while (!parser.nextIsEndObject()) {
            auto key = parser.nextKey();
            if (key == "value") {
                parser.readTo(value);
            } else if (key == "name") {
                parser.readTo(name);
            } else {
                parser.skipValue();
            }
        }
        parser.endObject();
        return parser.nextPosition();
    }

    /// @brief 同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const CustomFormatType& other) const {
        return value == other.value && name == other.name;
    }
};

/// @brief HasWriteFormat版getJsonContentのテスト。
TEST(HasReadWriteFormatTest, GetJsonContent) {
    CustomFormatType obj;
    obj.value = 42;
    obj.name = "test";
    auto json = getJsonContent(obj);
    EXPECT_EQ(json, "{value:42,name:\"test\"}");
}

/// @brief HasReadFormat版readJsonStringのテスト。
TEST(HasReadWriteFormatTest, ReadJsonString) {
    CustomFormatType obj;
    readJsonString("{value:123,name:\"hello\"}", obj);
    EXPECT_EQ(obj.value, 123);
    EXPECT_EQ(obj.name, "hello");
}

/// @brief HasReadFormat版readJsonFileのテスト。
TEST(HasReadWriteFormatTest, ReadJsonFile) {
    // 一時ファイルにJSONを書き込む
    std::string filename = "test_custom_format.json";
    {
        std::ofstream ofs(filename);
        ofs << "{value:999,name:\"from_file\"}";
    }

    // ファイルから読み込む
    CustomFormatType obj;
    readJsonFile(filename, obj);

    EXPECT_EQ(obj.value, 999);
    EXPECT_EQ(obj.name, "from_file");

    // 一時ファイルを削除
    std::remove(filename.c_str());
}

/// @brief HasReadFormat/HasWriteFormat版readJsonFileのラウンドトリップテスト。
TEST(HasReadWriteFormatTest, RoundTrip) {
    CustomFormatType original;
    original.value = 42;
    original.name = "test";
    testJsonRoundTrip(original, "{value:42,name:\"test\"}");
}

// ********************************************************************************
// ObjectSerializerConverter テスト
// ********************************************************************************

struct JsonIOConverterTestType {
    int x = 0;
    std::string y;

    bool equals(const JsonIOConverterTestType& other) const {
        return x == other.x && y == other.y;
    }
};

static auto makeJsonIOConverter() {
    static const auto fields = getFieldSet(
        getRequiredField(&JsonIOConverterTestType::x, "x"),
        getRequiredField(&JsonIOConverterTestType::y, "y")
    );
    return getObjectSerializerConverter<JsonIOConverterTestType>(fields);
}

/// @brief Converter版 getJsonContent/readJsonString のテスト。
TEST(JsonIOConverterTest, ReadWriteWithConverter) {
    JsonIOConverterTestType original;
    original.x = 7;
    original.y = "converter";

    testJsonRoundTrip(original, "{x:7,y:\"converter\"}",
        makeJsonIOConverter());
}

/// @brief Converter版 readJsonFile のテスト。
TEST(JsonIOConverterTest, ReadJsonFileWithConverter) {
    std::string filename = "test_converter.json";
    {
        std::ofstream ofs(filename);
        ofs << "{x:21,y:\"file\"}";
    }

    JsonIOConverterTestType actual;
    readJsonFile(filename, actual, makeJsonIOConverter());

    EXPECT_EQ(actual.x, 21);
    EXPECT_EQ(actual.y, "file");
    std::remove(filename.c_str());
}

// ********************************************************************************
// Tests: ensure element-converter selection is used for variant/unique_ptr
// ********************************************************************************

struct RWElement {
    int x = 0;

    const ObjectSerializer& serializer() const {
        static const auto fields = getFieldSet(
            getRequiredField(&RWElement::x, "x")
        );
        return fields;
    }

    bool operator==(const RWElement& other) const {
        return x == other.x;
    }
};

TEST(JsonElementConverterTest, UniquePtrUsesElementConverter) {
    struct Holder {
        std::unique_ptr<RWElement> item;
        const ObjectSerializer& serializer() const {
            static const auto pointerConverter =
                getPointerConverter<decltype(item)>();
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::item, "item", pointerConverter)
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            if (item == nullptr || other.item == nullptr) {
                return item == other.item;
            }
            return *item == *other.item;
        }
        bool equals(const Holder& other) const {
            return *this == other;
        }
    };

    Holder original;
    original.item = std::make_unique<RWElement>();
    original.item->x = 21;
    testJsonRoundTrip(original, "{item:{x:21}}" );
}

TEST(JsonElementConverterTest, SharedPtrUsesPointerConverter) {
    struct Holder {
        std::shared_ptr<RWElement> item;
        const ObjectSerializer& serializer() const {
            static const auto pointerConverter =
                getPointerConverter<decltype(item)>();
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::item, "item", pointerConverter)
            );
            return fields;
        }
        bool equals(const Holder& other) const {
            if (item == nullptr || other.item == nullptr) {
                return item == other.item;
            }
            return *item == *other.item;
        }
    };

    Holder original;
    original.item = std::make_shared<RWElement>();
    original.item->x = 31;
    testJsonRoundTrip(original, "{item:{x:31}}" );
}

TEST(JsonElementConverterTest, CustomPointerUsesPointerConverterFactory) {
    struct Holder {
        NonStdOwningPtr<RWElement> item;
        const ObjectSerializer& serializer() const {
            static const auto pointerConverter =
                getPointerConverter<decltype(item)>(
                    getConverter<RWElement>(),
                    [](JsonParser& parser, RWElement&& value) {
                        return NonStdOwningPtr<RWElement>(
                            new RWElement(std::move(value)));
                    });
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::item, "item", pointerConverter)
            );
            return fields;
        }
        bool equals(const Holder& other) const {
            if (item == nullptr || other.item == nullptr) {
                return item == nullptr && other.item == nullptr;
            }
            return *item == *other.item;
        }
    };

    Holder original;
    original.item = NonStdOwningPtr<RWElement>(new RWElement());
    (*original.item).x = 41;
    testJsonRoundTrip(original, "{item:{x:41}}" );
}

TEST(JsonElementConverterTest, VariantUsesElementConverter) {
    struct Holder {
        std::variant<int, RWElement> v;
        const ObjectSerializer& serializer() const {
            static const auto converter = getVariantConverter<decltype(v)>();
            static const auto fields = getFieldSet(
                getRequiredField(&Holder::v, "v", converter)
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            return v == other.v;
        }
        bool equals(const Holder& other) const {
            return *this == other;
        }
    };

    Holder original;
    original.v = RWElement{42};
    testJsonRoundTrip(original, "{v:{x:42}}" );
}

TEST(JsonElementConverterTest, VariantElementConverterDerivedCustomizesString) {
    using Var = std::variant<std::string, RWElement>;

    struct MyElemConv : VariantElementConverter<Var> {
        using VariantElementConverter<Var>::write; // bring base template into scope for other types

        void write(JsonWriter& writer, const Var& value) const {
            std::visit([&](const auto& inner) {
                this->write(writer, inner);
            }, value);
        }

        void write(JsonWriter& writer, const std::string& value) const {
            // Prefix strings with marker so we can detect customization
            std::string tmp;
            tmp.reserve(4 + value.size());
            tmp = "PFX:";
            tmp += value;
            writer.writeObject(tmp);
        }

        void write(JsonWriter& writer, const RWElement& value) const {
            VariantElementConverter<Var>::template write<RWElement>(writer, value);
        }

        Var readString(JsonParser& parser) const {
            std::string s;
            parser.readTo(s);
            if (s.rfind("PFX:", 0) != 0) {
                throw std::runtime_error("Expected prefixed string");
            }
            return Var{ s.substr(4) };
        }
    };

    struct Holder {
        Var v;
        const ObjectSerializer& serializer() const {
            static const MyElemConv elemConv{};
            static const auto conv = getVariantConverter<Var>(elemConv);
            static const auto fields = getFieldSet(
                getInitialAlwaysField(&Holder::v, "v", conv)
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            return v == other.v;
        }
        bool equals(const Holder& other) const {
            return *this == other;
        }
    };

    // String alternative is written with prefix
    Holder s;
    s.v = std::string("abc");
    testJsonRoundTrip(s, "{v:\"PFX:abc\"}");

    // Object alternative still works (RWElement)
    Holder o;
    o.v = RWElement{5};
    testJsonRoundTrip(o, "{v:{x:5}}" );
}
