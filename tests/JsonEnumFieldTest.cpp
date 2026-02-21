import rai.serialization.object_serializer;
import rai.serialization.field_serializer;
import rai.serialization.object_converter;
import rai.serialization.json_writer;
import rai.serialization.parser;
import rai.serialization.json_io;
import rai.serialization.test_helper;
#include <gtest/gtest.h>
#include <string>
#include <sstream>

using namespace rai::serialization;
using namespace rai::serialization::test;

enum class Color { Red, Green, Blue };

constexpr EnumEntry<Color> colorEntries[] = {
    { Color::Red,   "red" },
    { Color::Green, "green" },
    { Color::Blue,  "blue" }
};

struct CH {
    Color color = Color::Red;

    const ObjectSerializer& serializer() const {
        static const auto colorConverter = rai::serialization::getEnumConverter(colorEntries);
        static const auto fields = getFieldSet(
            rai::serialization::getRequiredField(&CH::color, "color", colorConverter)
        );
        return fields;
    }

    bool equals(const CH& o) const {
        return color == o.color;
    }
};

struct CH2 {
    Color color = Color::Red;

    const ObjectSerializer& serializer() const {
        static const auto colorConverter = rai::serialization::getEnumConverter<Color>({
            { Color::Red,   "red" },
            { Color::Green, "green" },
            { Color::Blue,  "blue" }
        });
        static const auto fields = getFieldSet(
            rai::serialization::getRequiredField(&CH2::color, "color", colorConverter)
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
