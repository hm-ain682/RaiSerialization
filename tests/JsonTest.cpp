import rai.json.json_field;
import rai.json.json_writer;
import rai.json.json_binding;
import rai.json.json_io;
#include <gtest/gtest.h>
#include <string>
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

/// @brief BとCのJSON書き出しをテストする。
TEST(JsonWriterTest, WriteBAndC) {
    B b; C c;
    auto bText = getJsonContent(b);
    ASSERT_FALSE(bText.empty());
    // ファイル出力が例外なく動作することのみ確認
    ASSERT_NO_THROW(writeJsonFile(c, "c.json"));
}

/// @brief Bを具体型として書き出すテスト。
/// @note テンプレート関数は実引数の型に基づいて解決されるため、
///       参照型であっても実際の型でjsonFields()が呼ばれる。
TEST(JsonWriterTest, WriteBDirectly) {
    B b;
    auto text = getJsonContent(b);
    EXPECT_FALSE(text.empty());
    // wとyが含まれることを確認（全体比較）
    // JSON5形式：キーに引用符なし、数値は整数として出力
    EXPECT_EQ(text, "{w:true,y:2}");
}

/// @brief Aを具体型として書き出すテスト。
TEST(JsonWriterTest, WriteADirectly) {
    A a;
    auto text = getJsonContent(a);
    EXPECT_FALSE(text.empty());
    // wとxが含まれることを確認（全体比較）
    // JSON5形式：キーに引用符なし
    EXPECT_EQ(text, "{w:true,x:1}");
}

/// @brief 文字列からBを読み込むテスト。
TEST(JsonReaderTest, ReadBFromString) {
    const std::string json = "{\"w\":true,\"y\":2.5}";
    B b;
    readJsonString(json, b);
    EXPECT_TRUE(b.w);
    EXPECT_FLOAT_EQ(b.y, 2.5f);
}

/// @brief 文字列からCを読み込むテスト。
TEST(JsonReaderTest, ReadCFromString) {
    const std::string json = "{\"w\":false,\"z\":\"hello\"}";
    C c; readJsonString(json, c);
    EXPECT_FALSE(c.w);
    EXPECT_EQ(c.z, "hello");
}

/// @brief 基底クラス参照経由で仮想関数が呼ばれることを確認するテスト。
TEST(JsonWriterTest, VirtualDispatchFromBaseReference) {
    B b;
    b.w = false;
    b.y = 3.14f;

    // 基底クラスポインタ経由でアクセス
    A* basePtr = &b;
    auto text = getJsonContent(*basePtr);

    EXPECT_FALSE(text.empty());
    // Bのjsonfields()が呼ばれるので、wとyが含まれる（全体比較）
    // JSON5形式：キーに引用符なし
    EXPECT_EQ(text, "{w:false,y:3.14}");
}

/// @brief 基底クラス参照経由での読み込みテスト。
TEST(JsonReaderTest, VirtualDispatchRead) {
    const std::string json = "{\"w\":true,\"y\":2.5}";
    B b;
    A& baseRef = b;

    // 基底クラス参照経由で読み込み
    readJsonString(json, baseRef);

    EXPECT_TRUE(b.w);
    EXPECT_FLOAT_EQ(b.y, 2.5f);
}
