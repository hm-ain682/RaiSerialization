import rai.json.json_field;
import rai.json.json_polymorphic;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_field_set;
import rai.json.json_io;
import rai.json.test_helper;
import rai.json.json_token_manager;
import rai.collection.sorted_hash_array_map;
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <array>
#include <set>

using namespace rai::json;
using namespace rai::json::test;

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
            makeJsonField(&A::w, "w"),
            makeJsonField(&A::x, "x")
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
            makeJsonField(&A::w, "w"),
            makeJsonField(&B::y, "y")
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
            makeJsonField(&A::w, "w"),
            makeJsonField(&C::z, "z")
        );
        return fields;
    }
};

// ********************************************************************************
// カスタム判別キーを使ったポリモーフィックなフィールド/配列のテスト
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
            makeJsonField(&POne::x, "x")
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
            makeJsonField(&PTwo::s, "s")
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
            makeJsonPolymorphicField(&Holder::item, "item", pbEntriesMap, "kind"),
            makeJsonPolymorphicArrayField(&Holder::arr, "arr", pbEntriesMap, "kind")
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

// ********************************************************************************
// JsonField の既定値と書き出し省略に関するテスト
// ********************************************************************************

struct DefaultFieldTest {
    int a = 0;
    int b = 0;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<DefaultFieldTest>(
            makeJsonField(&DefaultFieldTest::a, "a"),
            makeJsonFieldWithDefault(&DefaultFieldTest::b, "b", 42)
        );
        return fields;
    }
};

struct SkipFieldTest {
    int a = 1;
    int b = 0;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<SkipFieldTest>(
            makeJsonField(&SkipFieldTest::a, "a"),
            makeJsonFieldSkipIfEqual(&SkipFieldTest::b, "b", 0)
        );
        return fields;
    }
};

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
            makeJsonField(&IntegerTypes::s, "s"),
            makeJsonField(&IntegerTypes::us, "us"),
            makeJsonField(&IntegerTypes::i, "i"),
            makeJsonField(&IntegerTypes::ui, "ui"),
            makeJsonField(&IntegerTypes::l, "l"),
            makeJsonField(&IntegerTypes::ul, "ul"),
            makeJsonField(&IntegerTypes::ll, "ll"),
            makeJsonField(&IntegerTypes::ull, "ull")
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

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<FloatingPointTypes>(
            makeJsonField(&FloatingPointTypes::f, "f"),
            makeJsonField(&FloatingPointTypes::d, "d"),
            makeJsonField(&FloatingPointTypes::ld, "ld")
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

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<CharacterTypes>(
            makeJsonField(&CharacterTypes::c, "c"),
            makeJsonField(&CharacterTypes::sc, "sc"),
            makeJsonField(&CharacterTypes::uc, "uc"),
            makeJsonField(&CharacterTypes::c8, "c8"),
            makeJsonField(&CharacterTypes::c16, "c16"),
            makeJsonField(&CharacterTypes::c32, "c32"),
            makeJsonField(&CharacterTypes::wc, "wc")
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

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<TestHolder>(
            makeJsonField(&TestHolder::c16, "c16")
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

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<NestedChild>(
            makeJsonField(&NestedChild::value, "value"),
            makeJsonField(&NestedChild::name, "name")
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
            makeJsonField(&NestedParent::child, "child"),
            makeJsonField(&NestedParent::flag, "flag")
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

// ********************************************************************************
// テストカテゴリ：ポインタとポインタのvector
// ********************************************************************************

/// @brief ポインタを含む構造体。
struct PointerHolder {
    std::unique_ptr<int> ptr;
    std::vector<std::unique_ptr<std::string>> ptrVec;

    const IJsonFieldSet& jsonFields() const {
            // Provide explicit element/container converter for vector of unique_ptr<string>
            using Element = std::unique_ptr<std::string>;
            auto& elemConv = getUniquePtrConverter<Element>();
            static const ContainerConverter<std::vector<Element>, std::remove_cvref_t<decltype(elemConv)>>
                containerConverter;
            static const auto fields = makeJsonFieldSet<PointerHolder>(
                makeJsonUniquePtrField(&PointerHolder::ptr, "ptr"),
                makeJsonContainerField(&PointerHolder::ptrVec, "ptrVec", containerConverter)
            );
            return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const PointerHolder& other) const {
        bool ptrMatch = (ptr == nullptr && other.ptr == nullptr) ||
                        (ptr != nullptr && other.ptr != nullptr && *ptr == *other.ptr);
        if (!ptrMatch) {
            return false;
        }

        if (ptrVec.size() != other.ptrVec.size()) {
            return false;
        }
        for (size_t i = 0; i < ptrVec.size(); ++i) {
            bool elemMatch = (ptrVec[i] == nullptr && other.ptrVec[i] == nullptr) ||
                             (ptrVec[i] != nullptr && other.ptrVec[i] != nullptr &&
                              *ptrVec[i] == *other.ptrVec[i]);
            if (!elemMatch) {
                return false;
            }
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

    testJsonRoundTrip(original, "{ptr:999,ptrVec:[\"first\",null,\"third\"]}");
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

    const IJsonFieldSet& jsonFields() const {
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
            void write(JsonWriter& w, const DispatchValue& v) const
            {
                std::visit([&w](const auto& val) {
                    w.writeObject(val);
                }, v.data);
            }
        };

        auto tokenConv = FromConv();
        static const TokenDispatchConverter<DispatchValue, FromConv> conv(tokenConv);
        static const auto fields = makeJsonFieldSet<TokenDispatchHolder>(
            makeJsonTokenDispatchField(&TokenDispatchHolder::value, "value", conv)
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
// HasReadJson/HasWriteJson テスト
// ********************************************************************************

/// @brief readJson/writeJsonメソッドを持つテスト用構造体。
struct CustomJsonType {
    int value = 0;
    std::string name;

    /// @brief JSONへの書き出し。
    /// @param writer JsonWriterの参照。
    void writeJson(JsonWriter& writer) const {
        writer.startObject();
        writer.key("value");
        writer.writeObject(value);
        writer.key("name");
        writer.writeObject(name);
        writer.endObject();
    }

    /// @brief JSONからの読み込み。
    /// @param parser JsonParserの参照。
    void readJson(JsonParser& parser) {
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
    }

    /// @brief 同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const CustomJsonType& other) const {
        return value == other.value && name == other.name;
    }
};

/// @brief HasWriteJson版getJsonContentのテスト。
TEST(HasReadWriteJsonTest, GetJsonContent) {
    CustomJsonType obj;
    obj.value = 42;
    obj.name = "test";
    auto json = getJsonContent(obj);
    EXPECT_EQ(json, "{value:42,name:\"test\"}");
}

/// @brief HasReadJson版readJsonStringのテスト。
TEST(HasReadWriteJsonTest, ReadJsonString) {
    CustomJsonType obj;
    readJsonString("{value:123,name:\"hello\"}", obj);
    EXPECT_EQ(obj.value, 123);
    EXPECT_EQ(obj.name, "hello");
}

/// @brief HasReadJson版readJsonFileのテスト。
TEST(HasReadWriteJsonTest, ReadJsonFile) {
    // 一時ファイルにJSONを書き込む
    std::string filename = "test_custom_json.json";
    {
        std::ofstream ofs(filename);
        ofs << "{value:999,name:\"from_file\"}";
    }

    // ファイルから読み込む
    CustomJsonType obj;
    readJsonFile(filename, obj);

    EXPECT_EQ(obj.value, 999);
    EXPECT_EQ(obj.name, "from_file");

    // 一時ファイルを削除
    std::remove(filename.c_str());
}

/// @brief HasReadJson版readJsonFileのラウンドトリップテスト。
TEST(HasReadWriteJsonTest, RoundTrip) {
    CustomJsonType original;
    original.value = 42;
    original.name = "test";
    testJsonRoundTrip(original, "{value:42,name:\"test\"}");
}

// ********************************************************************************
// テストカテゴリ：JsonContainerField
// ********************************************************************************

/// @brief JsonContainerFieldのテスト用の単純なカスタム型。
struct Tag {
    std::string label;
    int priority = 0;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<Tag>(
            makeJsonField(&Tag::label, "label"),
            makeJsonField(&Tag::priority, "priority")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool operator==(const Tag& other) const {
        return label == other.label && priority == other.priority;
    }
};

/// @brief JsonContainerFieldをvectorで使用するテスト用構造体。
struct SetFieldVectorHolder {
    std::vector<Tag> tags;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<SetFieldVectorHolder>(
            makeJsonContainerField(&SetFieldVectorHolder::tags, "tags")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const SetFieldVectorHolder& other) const {
        return tags == other.tags;
    }
};

/// @brief JsonContainerFieldのvectorでの読み書きテスト。
TEST(JsonContainerFieldTest, VectorReadWriteRoundTrip) {
    SetFieldVectorHolder original;
    original.tags = {{"first", 1}, {"second", 2}, {"third", 3}};
    testJsonRoundTrip(original,
        "{tags:[{label:\"first\",priority:1},{label:\"second\",priority:2},"
        "{label:\"third\",priority:3}]}");
}

/// @brief JsonContainerFieldの空vectorでの読み書きテスト。
TEST(JsonContainerFieldTest, VectorEmptyReadWriteRoundTrip) {
    SetFieldVectorHolder original;
    original.tags = {};
    testJsonRoundTrip(original, "{tags:[]}");
}

/// @brief JsonContainerFieldをstd::setで使用するテスト用構造体。
struct SetFieldSetHolder {
    std::set<std::string> tags;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<SetFieldSetHolder>(
            makeJsonContainerField(&SetFieldSetHolder::tags, "tags")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const SetFieldSetHolder& other) const {
        return tags == other.tags;
    }
};

/// @brief JsonContainerFieldのstd::setでの読み書きテスト。
TEST(JsonContainerFieldTest, SetReadWriteRoundTrip) {
    SetFieldSetHolder original;
    original.tags = {"alpha", "beta", "gamma"};
    // std::setはソートされるため、出力順序もソート済み
    testJsonRoundTrip(original, "{tags:[\"alpha\",\"beta\",\"gamma\"]}");
}

/// @brief JsonContainerFieldの空std::setでの読み書きテスト。
TEST(JsonContainerFieldTest, SetEmptyReadWriteRoundTrip) {
    SetFieldSetHolder original;
    original.tags = {};
    testJsonRoundTrip(original, "{tags:[]}");
}

/// @brief JsonContainerFieldを複雑な要素型（オブジェクト）で使用するテスト用構造体。
struct Point {
    int x = 0;
    int y = 0;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<Point>(
            makeJsonField(&Point::x, "x"),
            makeJsonField(&Point::y, "y")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

/// @brief JsonContainerFieldを複雑な要素型で使用するテスト用構造体。
struct SetFieldObjectHolder {
    std::vector<Point> points;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<SetFieldObjectHolder>(
            makeJsonContainerField(&SetFieldObjectHolder::points, "points")
        );
        return fields;
    }

    /// @brief 他インスタンスとの同値判定。
    /// @param other 比較対象。
    /// @return 同値ならtrue。
    bool equals(const SetFieldObjectHolder& other) const {
        return points == other.points;
    }
};

/// @brief JsonContainerFieldの複雑な要素型での読み書きテスト。
TEST(JsonContainerFieldTest, ObjectElementReadWriteRoundTrip) {
    SetFieldObjectHolder original;
    original.points = {{1, 2}, {3, 4}, {5, 6}};
    testJsonRoundTrip(original, "{points:[{x:1,y:2},{x:3,y:4},{x:5,y:6}]}");
}

// ********************************************************************************
// Tests: ensure element-converter selection is used for container/variant/unique_ptr
// ********************************************************************************

struct RWElement {
    int x = 0;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<RWElement>(
            makeJsonField(&RWElement::x, "x")
        );
        return fields;
    }

    bool operator==(const RWElement& other) const { return x == other.x; }
};

TEST(JsonElementConverterTest, ContainerUsesElementConverter)
{
    struct Holder {
        std::vector<RWElement> v;
        const IJsonFieldSet& jsonFields() const {
            static const auto fields = makeJsonFieldSet<Holder>(
                makeJsonContainerField(&Holder::v, "v")
            );
            return fields;
        }
        bool operator==(const Holder& other) const { return v == other.v; }
        bool equals(const Holder& other) const { return *this == other; }
    };

    Holder original;
    RWElement e;
    e.x = 11;
    original.v.push_back(e);
    testJsonRoundTrip(original, "{v:[{x:11}]}");
}

TEST(JsonElementConverterTest, UniquePtrUsesElementConverter)
{
    struct Holder {
        std::unique_ptr<RWElement> item;
        const IJsonFieldSet& jsonFields() const {
            static const auto fields = makeJsonFieldSet<Holder>(
                makeJsonUniquePtrField(&Holder::item, "item")
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            if (item == nullptr || other.item == nullptr) {
                return item == other.item;
            }
            return *item == *other.item;
        }
        bool equals(const Holder& other) const { return *this == other; }
    };

    Holder original;
    original.item = std::make_unique<RWElement>();
    original.item->x = 21;
    testJsonRoundTrip(original, "{item:{x:21}}" );
}

TEST(JsonElementConverterTest, VariantUsesElementConverter)
{
    struct Holder {
        std::variant<int, RWElement> v;
        const IJsonFieldSet& jsonFields() const {
            static const auto fields = makeJsonFieldSet<Holder>(
                makeJsonVariantField(&Holder::v, "v")
            );
            return fields;
        }
        bool operator==(const Holder& other) const {
            return v == other.v;
        }
        bool equals(const Holder& other) const { return *this == other; }
    };

    Holder original;
    original.v = RWElement{42};
    testJsonRoundTrip(original, "{v:{x:42}}" );
}

TEST(JsonElementConverterTest, VariantElementConverterDerivedCustomizesString)
{
    using Var = std::variant<std::string, RWElement>;

    struct MyElemConv : VariantElementConverter<Var> {
        using VariantElementConverter<Var>::write; // bring base template into scope for other types

        void write(JsonWriter& writer, const std::string& value) const {
            // Prefix strings with marker so we can detect customization
            std::string tmp;
            tmp.reserve(4 + value.size());
            tmp = "PFX:";
            tmp += value;
            writer.writeObject(tmp);
        }
        void readString(JsonParser& parser, Var& value) const {
            std::string s;
            parser.readTo(s);
            if (s.rfind("PFX:", 0) != 0) {
                throw std::runtime_error("Expected prefixed string");
            }
            value = s.substr(4);
        }
    };

    struct Holder {
        Var v;
        const IJsonFieldSet& jsonFields() const {
            static const MyElemConv elemConv{};
            static const auto conv = makeVariantConverter<Var>(elemConv);
            static const auto fields = makeJsonFieldSet<Holder>(
                JsonField(&Holder::v, "v", std::cref(conv))
            );
            return fields;
        }
        bool operator==(const Holder& other) const { return v == other.v; }
        bool equals(const Holder& other) const { return *this == other; }
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

TEST(JsonElementConverterTest, NestedContainerUsesElementConverter)
{
    struct Holder {
        std::vector<std::vector<RWElement>> v;
        const IJsonFieldSet& jsonFields() const {
            // Explicitly construct nested container converter: inner element -> JsonFieldsConverter<RWElement>
            using Converter = JsonFieldsConverter<RWElement>;
            static const Converter innerElemConv{};
            using RWElementVector = std::vector<RWElement>;
            static const ContainerConverter<RWElementVector, Converter> innerConv(innerElemConv);
            static const ContainerConverter<std::vector<RWElementVector>, decltype(innerConv)>
                conv(innerConv);
            static const auto fields = makeJsonFieldSet<Holder>(
                makeJsonContainerField(&Holder::v, "v", conv)
            );
            return fields;
        }
        bool operator==(const Holder& other) const { return v == other.v; }
        bool equals(const Holder& other) const { return *this == other; }
    };

    Holder original;
    original.v.push_back({RWElement{1}, RWElement{2}});
    testJsonRoundTrip(original, "{v:[[{x:1},{x:2}]]}" );
}

TEST(JsonElementConverterExplicitTest, ContainerOfEnumWithExplicitContainerConverter)
{
    enum class Color { Red, Blue };
    constexpr EnumEntry<Color> entries[] = {{Color::Red, "Red"}, {Color::Blue, "Blue"}};
    using MapType = JsonEnumMap<Color, 2>;
    static const MapType cmap = makeJsonEnumMap(entries);

    struct Holder {
        std::vector<Color> v;
        const IJsonFieldSet& jsonFields() const {
            static const EnumConverter<MapType> econv(cmap);
            static const ContainerConverter<std::vector<Color>, EnumConverter<MapType>> conv(econv);
            static const auto fields = makeJsonFieldSet<Holder>(
                makeJsonContainerField(&Holder::v, "v", conv)
            );
            return fields;
        }
        bool operator==(const Holder& other) const { return v == other.v; }
        bool equals(const Holder& other) const { return *this == other; }
    };

    Holder original;
    original.v = {Color::Red, Color::Blue};
    testJsonRoundTrip(original, "{v:[\"Red\",\"Blue\"]}");
}

TEST(JsonElementConverterExplicitTest, ContainerWithExplicitElementConverter)
{
    struct Holder {
        std::vector<RWElement> v;
        const IJsonFieldSet& jsonFields() const {
            // Provide an explicit container converter instance (use ContainerConverter to avoid copying element converters)
            static const JsonFieldsConverter<RWElement> elemConv{};
            static const ContainerConverter<std::vector<RWElement>, JsonFieldsConverter<RWElement>> conv(elemConv);
            static const auto fields = makeJsonFieldSet<Holder>(
                makeJsonContainerField(&Holder::v, "v", conv)
            );
            return fields;
        }
        bool operator==(const Holder& other) const { return v == other.v; }
        bool equals(const Holder& other) const { return *this == other; }
    };

    Holder original;
    RWElement e;
    e.x = 11;
    original.v.push_back(e);
    testJsonRoundTrip(original, "{v:[{x:11}]}");
}
