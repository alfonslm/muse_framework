/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "serialization/json.h"
#include "types/bytearray.h"

using namespace muse;

class Global_Ser_Json : public ::testing::Test
{
public:
};

TEST_F(Global_Ser_Json, WriteRead)
{
    ByteArray data;

    // Write
    {
        JsonObject root;

        root["hello"] = "world";
        root["t"] = true;
        root["f"] = false;
        root["n"] = JsonValue();
        root["i"] = 123;
        root["pi"] = 3.1416;

        JsonArray ar;
        ar.append(1);
        ar.append(2);
        ar.append(3);
        ar.append(4);
        root["a"] = ar;

        JsonObject o;
        o["key1"] = "val1";
        o["key2"] = "val2";
        root["o"] = o;

        data = JsonDocument(root).toJson(JsonDocument::Format::Indented);
    }

    std::cout << data.constChar() << std::endl;

    // Read
    {
        JsonDocument doc = JsonDocument::fromJson(data);

        EXPECT_TRUE(doc.isObject());

        JsonObject root = doc.rootObject();

        EXPECT_FALSE(root.contains("notexists"));

        EXPECT_TRUE(root.contains("hello"));
        EXPECT_TRUE(root.value("hello").isString());
        EXPECT_EQ(root.value("hello").toString(), u"world");

        EXPECT_TRUE(root.contains("t"));
        EXPECT_TRUE(root.value("t").isBool());
        EXPECT_TRUE(root.value("t").toBool());

        EXPECT_TRUE(root.contains("f"));
        EXPECT_TRUE(root.value("f").isBool());
        EXPECT_FALSE(root.value("f").toBool());

        EXPECT_TRUE(root.contains("n"));
        EXPECT_TRUE(root.value("n").isNull());

        EXPECT_TRUE(root.contains("i"));
        EXPECT_TRUE(root.value("i").isNumber());
        EXPECT_EQ(root.value("i").toInt(), 123);

        EXPECT_TRUE(root.contains("pi"));
        EXPECT_TRUE(root.value("pi").isNumber());
        EXPECT_DOUBLE_EQ(root.value("pi").toDouble(), 3.1416);

        EXPECT_TRUE(root.contains("a"));
        EXPECT_TRUE(root.value("a").isArray());
        JsonArray ar = root.value("a").toArray();
        EXPECT_EQ(ar.size(), 4);
        EXPECT_EQ(ar.at(0).toInt(), 1);
        EXPECT_EQ(ar.at(3).toInt(), 4);

        EXPECT_TRUE(root.contains("o"));
        EXPECT_TRUE(root.value("o").isObject());
        JsonObject obj = root.value("o").toObject();
        EXPECT_EQ(obj.size(), 2);
        std::vector<std::string> keys = obj.keys();
        EXPECT_EQ(keys.size(), 2);
        EXPECT_EQ(keys.at(0), "key1");
        EXPECT_EQ(keys.at(1), "key2");
    }
}

static std::string toText(const JsonArray& arr, JsonDocument::Format format = JsonDocument::Format::Compact)
{
    ByteArray data = JsonDocument(arr).toJson(format);
    return std::string(data.constChar(), data.size());
}

TEST_F(Global_Ser_Json, Write_WholeNumbers_BeyondInt)
{
    // [GIVEN] Whole numbers that don't fit into 32 bits
    JsonArray arr;
    arr.append(0);
    arr.append(-7);
    arr.append(2147483648.0); // 2^31
    arr.append(5000000000.0);
    arr.append(-5000000000.0);
    arr.append(9007199254740991.0); // 2^53 - 1

    // [THEN] They are written in full, without a fraction or an exponent
    EXPECT_EQ(toText(arr), "[0,-7,2147483648,5000000000,-5000000000,9007199254740991]");
}

TEST_F(Global_Ser_Json, Write_Fractions_ShortestText)
{
    JsonArray arr;
    arr.append(0.5);
    arr.append(-0.25);
    arr.append(0.1);
    arr.append(3.1416);
    arr.append(61.125);
    arr.append(1.0 / 3.0);

    EXPECT_EQ(toText(arr), "[0.5,-0.25,0.1,3.1416,61.125,0.3333333333333333]");
}

TEST_F(Global_Ser_Json, WriteRead_Numbers_RoundTripExactly)
{
    // [GIVEN] Numbers that need more than 6 decimals, or are very large or very small
    const std::vector<double> numbers = {
        1.0 / 3.0,
        0.1 + 0.2,
        static_cast<double>(0.1f),
        -3.200000047683716,
        123456.789012345,
        0.0000001,
        0.000000123456789,
        1e20,
        -1.5e300,
        2.2250738585072014e-308,
        9007199254740993.0,
        5000000000.0,
        48000.0 * 3600.0 * 24.0 * 365.0,
    };

    JsonArray arr;
    for (double n : numbers) {
        arr.append(n);
    }

    for (JsonDocument::Format format : { JsonDocument::Format::Compact, JsonDocument::Format::Indented }) {
        // [WHEN] Written and read back
        ByteArray data = JsonDocument(arr).toJson(format);
        std::string err;
        JsonDocument doc = JsonDocument::fromJson(data, &err);

        // [THEN] Every number is the same, bit for bit
        ASSERT_TRUE(err.empty()) << err;
        ASSERT_TRUE(doc.isArray());
        JsonArray back = doc.rootArray();
        ASSERT_EQ(back.size(), numbers.size());
        for (size_t i = 0; i < numbers.size(); ++i) {
            EXPECT_TRUE(back.at(i).isNumber());
            EXPECT_EQ(back.at(i).toDouble(), numbers.at(i)) << "number " << i << " in " << data.constChar();
        }

        // [THEN] Writing what was read gives the same text
        ByteArray again = JsonDocument(back).toJson(format);
        EXPECT_EQ(std::string(again.constChar(), again.size()), std::string(data.constChar(), data.size()));
    }
}

TEST_F(Global_Ser_Json, Write_Layout)
{
    // [GIVEN]
    JsonObject inner;
    inner["k"] = 1.5;
    inner["s"] = "a \"quoted\" word";

    JsonArray arr;
    arr.append(1);
    arr.append(true);
    arr.append(JsonValue());
    arr.append(inner);
    arr.append(JsonArray());
    arr.append(JsonObject());

    // [THEN] Compact
    EXPECT_EQ(toText(arr), "[1,true,null,{\"k\":1.5,\"s\":\"a \\\"quoted\\\" word\"},[],{}]");

    // [THEN] Indented
    EXPECT_EQ(toText(arr, JsonDocument::Format::Indented),
              "[\n"
              "  1,\n"
              "  true,\n"
              "  null,\n"
              "  {\n"
              "    \"k\": 1.5,\n"
              "    \"s\": \"a \\\"quoted\\\" word\"\n"
              "  },\n"
              "  [],\n"
              "  {}\n"
              "]\n");
}
