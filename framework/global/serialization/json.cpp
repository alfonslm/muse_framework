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
#include "json.h"

#include <cmath>
#include <cassert>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sstream>

#define PICOJSON_USE_LOCALE 1
#define PICOJSON_ASSERT assert
#include "picojson.h"

#include "log.h"

using namespace muse;

struct muse::JsonData
{
    picojson::value val;
};

static inline const picojson::value& val_const(const std::shared_ptr<JsonData>& d)
{
    return d->val;
}

static inline picojson::value& val_mut(std::shared_ptr<JsonData>& d)
{
    return d->val;
}

// =======================================
// JsonValue
// =======================================
JsonValue::JsonValue(bool v)
    : m_data(std::make_shared<JsonData>())
{
    val_mut(m_data).set<bool>(v);
}

JsonValue::JsonValue(int v)
    : m_data(std::make_shared<JsonData>())
{
    double d = static_cast<double>(v);
    val_mut(m_data).set<double>(d);
}

JsonValue::JsonValue(double v)
    : m_data(std::make_shared<JsonData>())
{
    val_mut(m_data).set<double>(v);
}

JsonValue::JsonValue(const String& v)
    : m_data(std::make_shared<JsonData>())
{
    val_mut(m_data).set<std::string>(v.toStdString());
}

JsonValue::JsonValue(const std::string& v)
    : m_data(std::make_shared<JsonData>())
{
    val_mut(m_data).set<std::string>(v);
}

JsonValue::JsonValue(const char* v)
    : m_data(std::make_shared<JsonData>())
{
    val_mut(m_data).set<std::string>(std::string(v));
}

JsonValue::JsonValue(std::shared_ptr<JsonData> d)
    : m_data(d)
{
    if (!m_data) {
        m_data = std::make_shared<JsonData>();
    }
}

void JsonValue::detach()
{
    if (!m_data) {
        return;
    }

    if (m_data.use_count() == 1) {
        return;
    }

    m_data = std::make_shared<JsonData>(*m_data);
}

bool JsonValue::isNull() const
{
    return val_const(m_data).is<picojson::null>();
}

void JsonValue::setNull()
{
    detach();
    val_mut(m_data) = picojson::value();
}

bool JsonValue::isBool() const
{
    return val_const(m_data).is<bool>();
}

bool JsonValue::toBool() const
{
    return val_const(m_data).evaluate_as_boolean();
}

JsonValue& JsonValue::operator=(bool v)
{
    detach();
    val_mut(m_data).set<bool>(v);
    return *this;
}

bool JsonValue::isNumber() const
{
    return val_const(m_data).is<double>();
}

int JsonValue::toInt() const
{
    if (!isNumber()) {
        return 0;
    }
    return static_cast<int>(std::round(val_const(m_data).get<double>()));
}

double JsonValue::toDouble() const
{
    if (!isNumber()) {
        return 0.0;
    }
    return val_const(m_data).get<double>();
}

JsonValue& JsonValue::operator=(int v)
{
    detach();
    double d = static_cast<double>(v);
    val_mut(m_data).set<double>(d);
    return *this;
}

JsonValue& JsonValue::operator=(double v)
{
    detach();
    val_mut(m_data).set<double>(v);
    return *this;
}

bool JsonValue::isString() const
{
    return val_const(m_data).is<std::string>();
}

String JsonValue::toString() const
{
    if (!isString()) {
        static String dummy;
        return dummy;
    }
    return String::fromStdString(val_const(m_data).get<std::string>());
}

const std::string& JsonValue::toStdString() const
{
    if (!isString()) {
        static std::string dummy;
        return dummy;
    }
    return val_const(m_data).get<std::string>();
}

JsonValue& JsonValue::operator=(const String& str)
{
    detach();
    val_mut(m_data).set<std::string>(str.toStdString());
    return *this;
}

JsonValue& JsonValue::operator=(const std::string& str)
{
    detach();
    val_mut(m_data).set<std::string>(str);
    return *this;
}

JsonValue& JsonValue::operator=(const char* str)
{
    detach();
    val_mut(m_data).set<std::string>(std::string(str));
    return *this;
}

bool JsonValue::isArray() const
{
    return val_const(m_data).is<picojson::array>();
}

JsonArray JsonValue::toArray() const
{
    return JsonArray(m_data);
}

JsonValue& JsonValue::operator=(const JsonArray& arr)
{
    detach();
    val_mut(m_data) = val_const(arr.m_data);
    return *this;
}

bool JsonValue::isObject() const
{
    return val_const(m_data).is<picojson::object>();
}

JsonObject JsonValue::toObject() const
{
    return JsonObject(m_data);
}

JsonValue& JsonValue::operator=(const JsonObject& obj)
{
    detach();
    val_mut(m_data) = val_const(obj.m_data);
    return *this;
}

// =======================================
// JsonValueRef
// =======================================

JsonValueRef::JsonValueRef(const std::string& key, JsonObject* o)
    : m_key(key), m_object(o)
{
}

JsonValueRef::JsonValueRef(size_t i, JsonArray* a)
    : m_idx(i), m_array(a)
{
}

JsonValueRef& JsonValueRef::operator=(bool v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(int v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(double v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(const String& v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(const std::string& v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(const char* v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(const JsonValue& v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(const JsonArray& v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

JsonValueRef& JsonValueRef::operator=(const JsonObject& v)
{
    if (m_object) {
        m_object->set(m_key, v);
    } else if (m_array) {
        m_array->set(m_idx, v);
    }
    return *this;
}

// =======================================
// JsonArray
// =======================================

static inline const picojson::array& array_const(const std::shared_ptr<JsonData>& d)
{
    return d->val.get<picojson::array>();
}

static inline picojson::array& array_mut(std::shared_ptr<JsonData>& d)
{
    return d->val.get<picojson::array>();
}

JsonArray::JsonArray(std::shared_ptr<JsonData> d)
    : m_data(d)
{
    if (!m_data) {
        m_data = std::make_shared<JsonData>();
        m_data->val = picojson::value(picojson::array());
    }
}

JsonArray::JsonArray(std::initializer_list<JsonValue> args)
{
    m_data = std::make_shared<JsonData>();
    m_data->val = picojson::value(picojson::array());

    array_mut(m_data).reserve(args.size());
    for (const JsonValue& v : args) {
        array_mut(m_data).push_back(v.m_data->val);
    }
}

void JsonArray::detach()
{
    if (!m_data) {
        return;
    }

    if (m_data.use_count() == 1) {
        return;
    }

    m_data = std::make_shared<JsonData>(*m_data);
}

size_t JsonArray::size() const
{
    return array_const(m_data).size();
}

void JsonArray::resize(size_t i)
{
    detach();
    array_mut(m_data).resize(i);
}

bool JsonArray::empty() const
{
    return array_const(m_data).size() == 0;
}

JsonValue JsonArray::at(size_t i) const
{
    std::shared_ptr<JsonData> d = std::make_shared<JsonData>();
    d->val = array_const(m_data).at(i);
    return JsonValue(d);
}

JsonArray& JsonArray::set(size_t i, bool v)
{
    detach();
    array_mut(m_data)[i] = picojson::value(v);
    return *this;
}

JsonArray& JsonArray::set(size_t i, int v)
{
    detach();
    array_mut(m_data)[i] = picojson::value(static_cast<double>(v));
    return *this;
}

JsonArray& JsonArray::set(size_t i, double v)
{
    detach();
    array_mut(m_data)[i] = picojson::value(v);
    return *this;
}

JsonArray& JsonArray::set(size_t i, const String& str)
{
    detach();
    array_mut(m_data)[i] = picojson::value(str.toStdString());
    return *this;
}

JsonArray& JsonArray::set(size_t i, const std::string& str)
{
    detach();
    array_mut(m_data)[i] = picojson::value(str);
    return *this;
}

JsonArray& JsonArray::set(size_t i, const char* str)
{
    detach();
    array_mut(m_data)[i] = picojson::value(std::string(str));
    return *this;
}

JsonArray& JsonArray::set(size_t i, const JsonValue& v)
{
    detach();
    array_mut(m_data)[i] = v.m_data->val;
    return *this;
}

JsonArray& JsonArray::set(size_t i, const JsonArray& v)
{
    detach();
    array_mut(m_data)[i] = v.m_data->val;
    return *this;
}

JsonArray& JsonArray::set(size_t i, const JsonObject& v)
{
    detach();
    array_mut(m_data)[i] = v.m_data->val;
    return *this;
}

JsonArray& JsonArray::append(bool v)
{
    detach();
    array_mut(m_data).push_back(picojson::value(v));
    return *this;
}

JsonArray& JsonArray::append(int v)
{
    detach();
    array_mut(m_data).push_back(picojson::value(static_cast<double>(v)));
    return *this;
}

JsonArray& JsonArray::append(double v)
{
    detach();
    array_mut(m_data).push_back(picojson::value(v));
    return *this;
}

JsonArray& JsonArray::append(const String& str)
{
    detach();
    array_mut(m_data).push_back(picojson::value(str.toStdString()));
    return *this;
}

JsonArray& JsonArray::append(const std::string& str)
{
    detach();
    array_mut(m_data).push_back(picojson::value(str));
    return *this;
}

JsonArray& JsonArray::append(const char* str)
{
    detach();
    array_mut(m_data).push_back(picojson::value(std::string(str)));
    return *this;
}

JsonArray& JsonArray::append(const JsonValue& v)
{
    detach();
    array_mut(m_data).push_back(val_const(v.m_data));
    return *this;
}

JsonArray& JsonArray::append(const JsonArray& v)
{
    detach();
    array_mut(m_data).push_back(val_const(v.m_data));
    return *this;
}

JsonArray& JsonArray::append(const JsonObject& v)
{
    detach();
    array_mut(m_data).push_back(val_const(v.m_data));
    return *this;
}

JsonValue JsonArray::operator [](size_t i) const
{
    return at(i);
}

JsonValueRef JsonArray::operator [](size_t i)
{
    return JsonValueRef(i, this);
}

// =======================================
// JsonObject
// =======================================
static const picojson::object& object_const(const std::shared_ptr<JsonData>& d)
{
    return d->val.get<picojson::object>();
}

static picojson::object& object_mut(std::shared_ptr<JsonData>& d)
{
    return d->val.get<picojson::object>();
}

JsonObject::JsonObject(std::shared_ptr<JsonData> d)
    : m_data(d)
{
    if (!m_data) {
        m_data = std::make_shared<JsonData>();
        m_data->val = picojson::value(picojson::object());
    }
}

void JsonObject::detach()
{
    if (!m_data) {
        return;
    }

    if (m_data.use_count() == 1) {
        return;
    }

    m_data = std::make_shared<JsonData>(*m_data);
}

bool JsonObject::isValid() const
{
    return val_const(m_data).is<picojson::object>();
}

bool JsonObject::empty() const
{
    if (!isValid()) {
        return true;
    }

    return object_const(m_data).size() == 0;
}

size_t JsonObject::size() const
{
    if (!isValid()) {
        return 0;
    }

    return object_const(m_data).size();
}

bool JsonObject::contains(const std::string& key) const
{
    if (!isValid()) {
        return false;
    }

    const picojson::object& o = object_const(m_data);
    return o.find(key) != o.cend();
}

JsonValue JsonObject::value(const std::string& key, JsonValue def) const
{
    if (!isValid()) {
        return def;
    }

    const picojson::object& o = object_const(m_data);
    auto it = o.find(key);
    if (it != o.cend()) {
        std::shared_ptr<JsonData> d = std::make_shared<JsonData>();
        d->val = it->second;
        return JsonValue(d);
    }
    return def;
}

JsonObject& JsonObject::set(const std::string& key, bool v)
{
    detach();
    object_mut(m_data).insert_or_assign(key, picojson::value(v));
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, int v)
{
    detach();
    object_mut(m_data).insert_or_assign(key, picojson::value(static_cast<double>(v)));
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, double v)
{
    detach();
    object_mut(m_data).insert_or_assign(key, picojson::value(v));
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, const std::string& v)
{
    detach();
    object_mut(m_data).insert_or_assign(key, picojson::value(v));
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, const char* str)
{
    detach();
    object_mut(m_data).insert_or_assign(key, picojson::value(std::string(str)));
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, const String& str)
{
    detach();
    object_mut(m_data).insert_or_assign(key, picojson::value(str.toStdString()));
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, const JsonValue& v)
{
    detach();
    object_mut(m_data).insert_or_assign(key, v.m_data->val);
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, const JsonArray& v)
{
    detach();
    object_mut(m_data).insert_or_assign(key, v.m_data->val);
    return *this;
}

JsonObject& JsonObject::set(const std::string& key, const JsonObject& v)
{
    detach();
    object_mut(m_data).insert_or_assign(key, v.m_data->val);
    return *this;
}

JsonValue JsonObject::operator [](const std::string& key) const
{
    return value(key);
}

JsonValueRef JsonObject::operator [](const std::string& key)
{
    return JsonValueRef(key, this);
}

std::vector<std::string> JsonObject::keys() const
{
    std::vector<std::string> result;
    const picojson::object& o = object_const(m_data);
    for (auto&& p : o) {
        result.push_back(p.first);
    }
    return result;
}

// =======================================
// JsonDocument
// =======================================
JsonDocument::JsonDocument(std::shared_ptr<JsonData> d)
    : m_data(d)
{
}

JsonDocument::JsonDocument(JsonObject o)
    : m_data(o.m_data)
{
}

JsonDocument::JsonDocument(JsonArray a)
    : m_data(a.m_data)
{
}

//! NOTE Numbers are written here, not by picojson, so that reading back what was written gives the same number:
//! - whole numbers in full (up to 2^53, the largest that a double holds exactly);
//! - other numbers with as few digits as it takes to read back the same double (0.1 is "0.1", 1.0 / 3.0 is "0.3333333333333333").
//! The output doesn't depend on the locale.
static std::string numberToString(double n)
{
    if (!std::isfinite(n)) {
        // JSON has no NaN or infinity
        return "null";
    }

    static constexpr double MAX_EXACT_WHOLE = 9007199254740992.0; // 2^53

    double whole = 0.0;
    if (std::fabs(n) < MAX_EXACT_WHOLE && std::modf(n, &whole) == 0.0) {
        return std::to_string(static_cast<long long>(n));
    }

    std::string str;
    for (int precision = 15; precision <= 17; ++precision) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(precision) << n;
        str = out.str();

        std::istringstream in(str);
        in.imbue(std::locale::classic());
        double back = 0.0;
        in >> back;
        if (back == n) {
            break;
        }
    }

    return str;
}

static constexpr int COMPACT = -1;

static void newLine(std::string& out, int indent)
{
    out += '\n';
    out.append(static_cast<size_t>(indent) * picojson::INDENT_WIDTH, ' ');
}

//! NOTE The same layout as picojson::value::serialize, except for numbers
static void serialize(const picojson::value& val, std::string& out, int indent)
{
    const bool indented = indent != COMPACT;

    if (val.is<picojson::array>()) {
        const picojson::array& arr = val.get<picojson::array>();
        const int inner = indented ? indent + 1 : COMPACT;

        out += '[';
        for (auto it = arr.begin(); it != arr.end(); ++it) {
            if (it != arr.begin()) {
                out += ',';
            }
            if (indented) {
                newLine(out, inner);
            }
            serialize(*it, out, inner);
        }
        if (indented && !arr.empty()) {
            newLine(out, indent);
        }
        out += ']';
    } else if (val.is<picojson::object>()) {
        const picojson::object& obj = val.get<picojson::object>();
        const int inner = indented ? indent + 1 : COMPACT;

        out += '{';
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it != obj.begin()) {
                out += ',';
            }
            if (indented) {
                newLine(out, inner);
            }
            picojson::serialize_str(it->first, std::back_inserter(out));
            out += ':';
            if (indented) {
                out += ' ';
            }
            serialize(it->second, out, inner);
        }
        if (indented && !obj.empty()) {
            newLine(out, indent);
        }
        out += '}';
    } else if (val.is<double>()) {
        out += numberToString(val.get<double>());
    } else {
        // null, bool, string
        val.serialize(std::back_inserter(out));
    }

    if (indent == 0) {
        out += '\n';
    }
}

ByteArray JsonDocument::toJson(Format format) const
{
    std::string json;
    serialize(m_data->val, json, format == Format::Indented ? 0 : COMPACT);
    return ByteArray(json.c_str(), json.size());
}

JsonDocument JsonDocument::fromJson(const ByteArray& ba, std::string* err)
{
    std::shared_ptr<JsonData> d = std::make_shared<JsonData>();

    std::string_view json(ba.constChar(), ba.size());

    const char* currentLoc = setlocale(LC_NUMERIC, "C");
    picojson::parse(d->val, json.begin(), json.end(), err);
    setlocale(LC_NUMERIC, currentLoc);

    JsonDocument doc(d);
    return doc;
}

bool JsonDocument::isObject() const
{
    return m_data->val.is<picojson::object>();
}

bool JsonDocument::isArray() const
{
    return m_data->val.is<picojson::array>();
}

JsonObject JsonDocument::rootObject() const
{
    return JsonObject(m_data);
}

JsonArray JsonDocument::rootArray() const
{
    return JsonArray(m_data);
}
