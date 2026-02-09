import rai.json.json_field_set;
import rai.json.json_field;
import rai.json.json_converter;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_io;
import rai.json.test_helper;
#include <gtest/gtest.h>
#include <string>
#include <sstream>

using namespace rai::json;
using namespace rai::json::test;

enum class Color { Red, Green, Blue };

constexpr EnumEntry<Color> colorEntries[] = {
    { Color::Red,   "red" },
    { Color::Green, "green" },
    { Color::Blue,  "blue" }
};

struct CH {
    Color color = Color::Red;

    const IJsonFieldSet& jsonFields() const {
        static const auto colorConverter = rai::json::getEnumConverter(colorEntries);
        static const auto fields = getFieldSet(
            rai::json::getRequiredField(&CH::color, "color", colorConverter)
        );
        return fields;
    }

    bool equals(const CH& o) const {
        return color == o.color;
    }
};

struct CH2 {
    Color color = Color::Red;

    const IJsonFieldSet& jsonFields() const {
        static const auto colorConverter = rai::json::getEnumConverter<Color>({
            { Color::Red,   "red" },
            { Color::Green, "green" },
            { Color::Blue,  "blue" }
        });
        static const auto fields = getFieldSet(
            rai::json::getRequiredField(&CH2::color, "color", colorConverter)
        );
        return fields;
    }

    bool equals(const CH2& o) const {
        return color == o.color;
    }
};

TEST(JsonEnumFieldStandalone, RoundTripWithHelper) {
    CH ch;
    ch.color = Color::Green;
    testJsonRoundTrip(ch, "{color:\"green\"}");
}

TEST(JsonEnumFieldStandalone, ReadUnknownValueThrows) {
    // Bad value not in enum entries should throw on read
    const std::string badJson = "{color:\"purple\"}";
    CH out{};
    EXPECT_THROW(readJsonString(badJson, out), std::runtime_error);
}

TEST(JsonEnumFieldStandalone, RoundTripWithVariadicHelper) {
    CH2 ch;
    ch.color = Color::Green;
    testJsonRoundTrip(ch, "{color:\"green\"}");
}

TEST(JsonEnumFieldStandalone, ReadUnknownValueThrowsWithVariadicHelper) {
    CH2 out{};
    EXPECT_THROW(readJsonString("{color:\"purple\"}", out), std::runtime_error);
}
