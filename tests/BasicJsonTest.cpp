import rai.json.json_writer;
#include <gtest/gtest.h>
#include <sstream>

using namespace rai::json;

TEST(BasicJsonWriter, SimpleObject) {
    std::ostringstream oss;
    JsonWriter writer(oss);
    writer.startObject();
    writer.key("x"); writer.writeObject(42);
    writer.key("s"); writer.writeObject(std::string_view("hi"));
    writer.endObject();

    EXPECT_EQ(oss.str(), "{x:42,s:\"hi\"}");
}
