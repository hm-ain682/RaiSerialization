import rai.json.json_field;
import rai.json.json_writer;
import rai.json.json_binding;
import rai.json.json_io;
import rai.collection.sorted_hash_array_map;
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <tuple>

using namespace rai::json;

/// @brief テスト用の構造体A。
struct A {
    bool w = true;
    int x = 1;

    virtual ~A() = default;

    /// @brief JSONフィールドを取得する仮想関数。
    /// @return フィールドプランへの参照。
    /// @note 戻り値はIJsonFieldSet&で、派生クラスでオーバーライド可能。
    ///       makeJsonFieldSetを使用することで型名を簡潔に記述。
    virtual const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<A>(
            JsonField(&A::w, "w"),
            JsonField(&A::x, "x")
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
    ///       makeJsonFieldSetを使用することで型名を簡潔に記述。
    const IJsonFieldSet& jsonFields() const override {
        static const auto fields = makeJsonFieldSet<B>(
            JsonField(&A::w, "w"),
            JsonField(&B::y, "y")
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
    ///       makeJsonFieldSetを使用することで型名を簡潔に記述。
    const IJsonFieldSet& jsonFields() const override {
        static const auto fields = makeJsonFieldSet<C>(
            JsonField(&A::w, "w"),
            JsonField(&C::z, "z")
        );
        return fields;
    }
};

// ********************************************************************************
// Polymorphic field/array tests for custom discriminator key
// ********************************************************************************

struct PB {
    virtual ~PB() = default;
    virtual const IJsonFieldSet& jsonFields() const {
        static const auto f = makeJsonFieldSet<PB>();
        return f;
    }

    /// @brief ポリモーフィックな比較演算子。
    /// @param other 比較対象のオブジェクト。
    /// @return 等しい場合はtrue、そうでない場合はfalse。
    virtual bool operator==(const PB& other) const = 0;
};

struct POne : public PB {
    int x = 0;
    const IJsonFieldSet& jsonFields() const override {
        static const auto f = makeJsonFieldSet<POne>(
            JsonField(&POne::x, "x")
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
    const IJsonFieldSet& jsonFields() const override {
        static const auto f = makeJsonFieldSet<PTwo>(
            JsonField(&PTwo::s, "s")
        );
        return f;
    }

    bool operator==(const PB& other) const override {
        auto* p = dynamic_cast<const PTwo*>(&other);
        return p != nullptr && s == p->s;
    }
};

using MapEntry = std::pair<std::string_view, std::function<std::unique_ptr<PB>()>>;

// entries を直接マップ構築（配列を経由せず簡潔に記述）
inline const auto pbEntriesMap = rai::collection::makeSortedHashArrayMap(
    MapEntry{ "One", []() { return std::make_unique<POne>(); } },
    MapEntry{ "Two", []() { return std::make_unique<PTwo>(); } }
);

struct Holder {
    std::unique_ptr<PB> item;
    std::vector<std::unique_ptr<PB>> arr;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<Holder>(
            JsonPolymorphicField(&Holder::item, "item", pbEntriesMap, "kind"),
            JsonPolymorphicArrayField(&Holder::arr, "arr", pbEntriesMap, "kind")
        );
        return fields;
    }

    bool operator==(const Holder& other) const {
        // item フィールドの比較
        bool itemMatch = (item == nullptr && other.item == nullptr) ||
            (item != nullptr && other.item != nullptr && *item == *other.item);
        if (!itemMatch) {
            return false;
        }

        // arr フィールドの比較
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

TEST(JsonPolymorphicTest, ReadSingleCustomKey) {
    // テスト用に値を設定
    Holder original;
    original.item = std::make_unique<POne>();
    dynamic_cast<POne*>(original.item.get())->x = 42;

    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, "{\"item\":{\"kind\":\"One\",\"x\":42}}");

    // JSONから読み込む
    Holder parsed;
    readJsonString(json, parsed);

    // 欠娃まれたエラー: 巩揃〺后をチェックしウ事根拙なければならない。
    // 元オブジェクトと比較（粗論的に検証）
    EXPECT_EQ(parsed, original);
}

TEST(JsonPolymorphicTest, ReadArrayCustomKeyAndNull) {
    // テスト用に値を設定
    Holder original;
    auto one = std::make_unique<POne>();
    one->x = 1;
    original.arr.push_back(std::move(one));

    auto two = std::make_unique<PTwo>();
    two->s = "abc";
    original.arr.push_back(std::move(two));

    original.arr.push_back(nullptr);

    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, "{\"arr\":[{\"kind\":\"One\",\"x\":1},{\"kind\":\"Two\",\"s\":\"abc\"},null]}");

    // JSONから読み込む
    Holder parsed;
    readJsonString(json, parsed);

    // 元オブジェクトと比較（粗論的に検証）
    EXPECT_EQ(parsed, original);
}

TEST(JsonPolymorphicTest, WriteAndReadRoundTripUsingCustomKey) {
    // テスト用に値を設定
    auto one = std::make_unique<POne>();
    one->x = 99;
    Holder original;
    original.item = std::move(one);

    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, "{\"item\":{\"kind\":\"One\",\"x\":99}}");

    // JSONから読み込む
    Holder parsed;
    readJsonString(json, parsed);

    // 元オブジェクトと比較（粗論的に検証）
    EXPECT_EQ(parsed, original);
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

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<IntegerTypes>(
            JsonField(&IntegerTypes::s, "s"),
            JsonField(&IntegerTypes::us, "us"),
            JsonField(&IntegerTypes::i, "i"),
            JsonField(&IntegerTypes::ui, "ui"),
            JsonField(&IntegerTypes::l, "l"),
            JsonField(&IntegerTypes::ul, "ul"),
            JsonField(&IntegerTypes::ll, "ll"),
            JsonField(&IntegerTypes::ull, "ull")
        );
        return fields;
    }

    bool operator==(const IntegerTypes& other) const {
        return s == other.s && us == other.us && i == other.i && ui == other.ui &&
               l == other.l && ul == other.ul && ll == other.ll && ull == other.ull;
    }
};

/// @brief 整数型の読み書きテスト。
TEST(JsonIntegerTest, ReadWriteRoundTrip) {
    // テスト用に異なる値を設定
    IntegerTypes original;
    original.s = -1000;
    original.us = 2000;
    original.i = -3000000;
    original.ui = 4000000;
    original.l = -5000000000LL;
    original.ul = 6000000000ULL;
    original.ll = 1234567890123456LL;
    original.ull = 9876543210987654ULL;

    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, "{s:-1000,"
        "us:2000,"
        "i:-3000000,"
        "ui:4000000,"
        "l:-5000000000,"
        "ul:6000000000,"
        "ll:1234567890123456,"
        "ull:9876543210987654}");

    // JSONから読み込む
    IntegerTypes parsed;
    readJsonString(json, parsed);

    // 値が一致していることを確認
    EXPECT_EQ(parsed, original);
}

// ********************************************************************************
// テストカテゴリ：浮動小数点数型
// ********************************************************************************

/// @brief 浮動小数点数型を含む構造体。
struct FloatingPointTypes {
    float f = 0.0f;
    double d = 0.0;
    long double ld = 0.0L;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<FloatingPointTypes>(
            JsonField(&FloatingPointTypes::f, "f"),
            JsonField(&FloatingPointTypes::d, "d"),
            JsonField(&FloatingPointTypes::ld, "ld")
        );
        return fields;
    }

    bool operator==(const FloatingPointTypes& other) const {
        return f == other.f && d == other.d && ld == other.ld;
    }
};

/// @brief 浮動小数点数型の読み書きテスト。
TEST(JsonFloatingPointTest, ReadWriteRoundTrip) {
    // テスト用に異なる値を設定
    FloatingPointTypes original;
    original.f = 1.5f;
    original.d = -2.75;
    original.ld = 3.125L;

    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, "{f:1.5,d:-2.75,ld:3.125}");

    // JSONから読み込む
    FloatingPointTypes parsed;
    readJsonString(json, parsed);

    // 値が一致していることを確認
    EXPECT_FLOAT_EQ(parsed.f, original.f);
    EXPECT_DOUBLE_EQ(parsed.d, original.d);
    EXPECT_DOUBLE_EQ(static_cast<double>(parsed.ld), static_cast<double>(original.ld));
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
    char16_t c16 = u'ア';
    char32_t c32 = U'🎉';
    wchar_t wc = L'ウ';

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<CharacterTypes>(
            JsonField(&CharacterTypes::c, "c"),
            JsonField(&CharacterTypes::sc, "sc"),
            JsonField(&CharacterTypes::uc, "uc"),
            JsonField(&CharacterTypes::c8, "c8"),
            JsonField(&CharacterTypes::c16, "c16"),
            JsonField(&CharacterTypes::c32, "c32"),
            JsonField(&CharacterTypes::wc, "wc")
        );
        return fields;
    }

    bool operator==(const CharacterTypes& other) const {
        return c == other.c && sc == other.sc && uc == other.uc && c8 == other.c8 &&
               c16 == other.c16 && c32 == other.c32 && wc == other.wc;
    }
};

/// @brief 文字型の読み書きテスト。
TEST(JsonCharacterTest, ReadWriteRoundTrip) {
    // テスト用に異なる値を設定
    CharacterTypes original;
    original.c = 'A';
    original.sc = 'B';
    original.uc = 'C';
    original.c8 = u8'd';
    original.c16 = u'イ';
    original.c32 = U'🌟';
    original.wc = L'エ';

    // JSON形式で書き出す
    auto json = getJsonContent(original);
    ASSERT_FALSE(json.empty());

    // JSONの内容が正しいか確認（全体比較）
    // 注: 文字型は escapeString で出力されるため、Unicode 文字は \uXXXX 形式
    // c16:u'イ' (U+30A4) → \u30a4
    // c32:U'🌟' (U+1F31F) → \ud80c\udf1f (サロゲートペア)
    // wc:L'エ' (U+30A8) → \u30a8
    EXPECT_EQ(json, "{c:\"A\",sc:\"B\",uc:\"C\","
        "c8:\"d\",c16:\"\\u30a4\",c32:\"\\ud80c\\udf1f\",wc:\"\\u30a8\"}");

    // JSONから読み込む
    CharacterTypes parsed;
    readJsonString(json, parsed);

    // 値が一致していることを確認
    EXPECT_EQ(parsed, original);
}

// ********************************************************************************
// テストカテゴリ：ネストされたオブジェクト
// ********************************************************************************

/// @brief ネストされたオブジェクト構造。
struct NestedChild {
    int value = 0;
    std::string name;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<NestedChild>(
            JsonField(&NestedChild::value, "value"),
            JsonField(&NestedChild::name, "name")
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

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<NestedParent>(
            JsonField(&NestedParent::child, "child"),
            JsonField(&NestedParent::flag, "flag")
        );
        return fields;
    }

    bool operator==(const NestedParent& other) const {
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

    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, "{child:{value:42,name:\"test\"},flag:true}");

    // JSONから読み込む
    NestedParent parsed;
    readJsonString(json, parsed);

    // 値が一致していることを確認
    EXPECT_EQ(parsed, original);
}

// ********************************************************************************
// テストカテゴリ：ポインタとポインタのvector
// ********************************************************************************

/// @brief ポインタを含む構造体。
struct PointerHolder {
    std::unique_ptr<int> ptr;
    std::vector<std::unique_ptr<std::string>> ptrVec;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<PointerHolder>(
            JsonField(&PointerHolder::ptr, "ptr"),
            JsonField(&PointerHolder::ptrVec, "ptrVec")
        );
        return fields;
    }

    bool operator==(const PointerHolder& other) const {
        bool ptrMatch = (ptr == nullptr && other.ptr == nullptr) ||
                        (ptr != nullptr && other.ptr != nullptr && *ptr == *other.ptr);
        if (!ptrMatch) return false;

        if (ptrVec.size() != other.ptrVec.size()) return false;
        for (size_t i = 0; i < ptrVec.size(); ++i) {
            bool elemMatch = (ptrVec[i] == nullptr && other.ptrVec[i] == nullptr) ||
                             (ptrVec[i] != nullptr && other.ptrVec[i] != nullptr &&
                              *ptrVec[i] == *other.ptrVec[i]);
            if (!elemMatch) return false;
        }
        return true;
    }
};

/// @brief ポインタとvectorの読み書きテスト。
TEST(JsonPointerTest, ReadWriteRoundTrip) {
    // テスト用に異なる値を設定
    PointerHolder original;
    original.ptr = std::make_unique<int>(999);
    original.ptrVec.push_back(std::make_unique<std::string>("first"));
    original.ptrVec.push_back(nullptr);
    original.ptrVec.push_back(std::make_unique<std::string>("third"));

    // JSON形式で書き出す
    auto json = getJsonContent(original);

    // JSONの内容が正しいか確認（全体比較）
    EXPECT_EQ(json, "{ptr:999,ptrVec:[\"first\",null,\"third\"]}");

    // JSONから読み込む
    PointerHolder parsed;
    readJsonString(json, parsed);

    // 値が一致していることを確認
    EXPECT_EQ(parsed, original);
}
