/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/FlyString.h>
#include <AK/String.h>
#include <LibJS/Heap/Heap.h>
#include <LibJS/Interpreter.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/BooleanObject.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/NumberObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/StringObject.h>
#include <LibJS/Runtime/Value.h>
#include <math.h>

namespace JS {

bool Value::is_array() const
{
    return is_object() && as_object().is_array();
}

String Value::to_string() const
{
    if (is_boolean())
        return as_bool() ? "true" : "false";

    if (is_null())
        return "null";

    if (is_undefined())
        return "undefined";

    if (is_number()) {
        if (is_nan())
            return "NaN";

        if (is_infinity())
            return as_double() < 0 ? "-Infinity" : "Infinity";

        // FIXME: This needs improvement.
        if ((double)to_i32() == as_double())
            return String::number(to_i32());
        return String::format("%f", as_double());
    }

    if (is_object())
        return as_object().to_primitive(Object::PreferredType::String).to_string();

    if (is_string())
        return m_value.as_string->string();

    ASSERT_NOT_REACHED();
}

bool Value::to_boolean() const
{
    switch (m_type) {
    case Type::Boolean:
        return m_value.as_bool;
    case Type::Number:
        if (is_nan()) {
            return false;
        }
        return !(m_value.as_double == 0 || m_value.as_double == -0);
    case Type::Null:
    case Type::Undefined:
        return false;
    case Type::String:
        return !as_string()->string().is_empty();
    case Type::Object:
        return true;
    default:
        ASSERT_NOT_REACHED();
    }
}

Object* Value::to_object(Heap& heap) const
{
    if (is_object())
        return &const_cast<Object&>(as_object());

    if (is_string())
        return heap.allocate<StringObject>(m_value.as_string);

    if (is_number())
        return heap.allocate<NumberObject>(m_value.as_double);

    if (is_boolean())
        return heap.allocate<BooleanObject>(m_value.as_bool);

    if (is_null() || is_undefined()) {
        heap.interpreter().throw_exception<Error>("TypeError", "ToObject on null or undefined.");
        return nullptr;
    }

    dbg() << "Dying because I can't to_object() on " << *this;
    ASSERT_NOT_REACHED();
}

Value Value::to_number() const
{
    switch (m_type) {
    case Type::Empty:
        ASSERT_NOT_REACHED();
        return {};
    case Type::Boolean:
        return Value(m_value.as_bool ? 1 : 0);
    case Type::Number:
        return Value(m_value.as_double);
    case Type::Null:
        return Value(0);
    case Type::String: {
        auto& string = as_string()->string();
        if (string.is_empty())
            return Value(0);
        bool ok;
        //FIXME: Parse in a better way
        auto parsed_int = string.to_int(ok);
        if (ok)
            return Value(parsed_int);

        return js_nan();
    }
    case Type::Undefined:
        return js_nan();
    case Type::Object:
        if (m_value.as_object->is_array()) {
            auto& array = *static_cast<Array*>(m_value.as_object);
            if (array.length() == 0)
                return Value(0);
            if (array.length() > 1)
                return js_nan();
            return array.elements()[0].to_number();
        } else {
            return m_value.as_object->to_primitive(Object::PreferredType::Number).to_number();
        }
    }

    ASSERT_NOT_REACHED();
}

i32 Value::to_i32() const
{
    return static_cast<i32>(to_number().as_double());
}

Value greater_than(Value lhs, Value rhs)
{
    return Value(lhs.to_number().as_double() > rhs.to_number().as_double());
}

Value greater_than_equals(Value lhs, Value rhs)
{
    return Value(lhs.to_number().as_double() >= rhs.to_number().as_double());
}

Value less_than(Value lhs, Value rhs)
{
    return Value(lhs.to_number().as_double() < rhs.to_number().as_double());
}

Value less_than_equals(Value lhs, Value rhs)
{
    return Value(lhs.to_number().as_double() <= rhs.to_number().as_double());
}

Value bitwise_and(Value lhs, Value rhs)
{
    return Value((i32)lhs.to_number().as_double() & (i32)rhs.to_number().as_double());
}

Value bitwise_or(Value lhs, Value rhs)
{
    bool lhs_invalid = lhs.is_undefined() || lhs.is_null() || lhs.is_nan() || lhs.is_infinity();
    bool rhs_invalid = rhs.is_undefined() || rhs.is_null() || rhs.is_nan() || rhs.is_infinity();

    if (lhs_invalid && rhs_invalid)
        return Value(0);

    if (lhs_invalid || rhs_invalid)
        return lhs_invalid ? rhs.to_number() : lhs.to_number();

    if (!rhs.is_number() && !lhs.is_number())
        return Value(0);

    return Value((i32)lhs.to_number().as_double() | (i32)rhs.to_number().as_double());
}

Value bitwise_xor(Value lhs, Value rhs)
{
    return Value((i32)lhs.to_number().as_double() ^ (i32)rhs.to_number().as_double());
}

Value bitwise_not(Value lhs)
{
    return Value(~(i32)lhs.to_number().as_double());
}

Value unary_plus(Value lhs)
{
    return lhs.to_number();
}

Value unary_minus(Value lhs)
{
    if (lhs.to_number().is_nan())
        return js_nan();
    return Value(-lhs.to_number().as_double());
}

Value left_shift(Value lhs, Value rhs)
{
    return Value((i32)lhs.to_number().as_double() << (i32)rhs.to_number().as_double());
}

Value right_shift(Value lhs, Value rhs)
{
    return Value((i32)lhs.to_number().as_double() >> (i32)rhs.to_number().as_double());
}

Value add(Value lhs, Value rhs)
{
    if (lhs.is_string() || rhs.is_string())
        return js_string((lhs.is_string() ? lhs : rhs).as_string()->heap(), String::format("%s%s", lhs.to_string().characters(), rhs.to_string().characters()));

    return Value(lhs.to_number().as_double() + rhs.to_number().as_double());
}

Value sub(Value lhs, Value rhs)
{
    return Value(lhs.to_number().as_double() - rhs.to_number().as_double());
}

Value mul(Value lhs, Value rhs)
{
    return Value(lhs.to_number().as_double() * rhs.to_number().as_double());
}

Value div(Value lhs, Value rhs)
{
    return Value(lhs.to_number().as_double() / rhs.to_number().as_double());
}

Value mod(Value lhs, Value rhs)
{
    if (lhs.to_number().is_nan() || rhs.to_number().is_nan())
        return js_nan();

    double index = lhs.to_number().as_double();
    double period = rhs.to_number().as_double();
    double trunc = (double)(i32) (index / period);

    return Value(index - trunc * period);
}

Value exp(Value lhs, Value rhs)
{
    return Value(pow(lhs.to_number().as_double(), rhs.to_number().as_double()));
}

Value typed_eq(Value lhs, Value rhs)
{
    if (rhs.type() != lhs.type())
        return Value(false);

    switch (lhs.type()) {
    case Value::Type::Empty:
        ASSERT_NOT_REACHED();
        return {};
    case Value::Type::Undefined:
        return Value(true);
    case Value::Type::Null:
        return Value(true);
    case Value::Type::Number:
        return Value(lhs.as_double() == rhs.as_double());
    case Value::Type::String:
        return Value(lhs.as_string()->string() == rhs.as_string()->string());
    case Value::Type::Boolean:
        return Value(lhs.as_bool() == rhs.as_bool());
    case Value::Type::Object:
        return Value(&lhs.as_object() == &rhs.as_object());
    }

    ASSERT_NOT_REACHED();
}

Value eq(Value lhs, Value rhs)
{
    if (lhs.type() == rhs.type())
        return typed_eq(lhs, rhs);

    if ((lhs.is_undefined() || lhs.is_null()) && (rhs.is_undefined() || rhs.is_null()))
        return Value(true);

    if (lhs.is_object() && rhs.is_boolean())
        return eq(lhs.as_object().to_primitive(), rhs.to_number());

    if (lhs.is_boolean() && rhs.is_object())
        return eq(lhs.to_number(), rhs.as_object().to_primitive());

    if (lhs.is_object())
        return eq(lhs.as_object().to_primitive(), rhs);

    if (rhs.is_object())
        return eq(lhs, rhs.as_object().to_primitive());

    if (lhs.is_number() || rhs.is_number())
        return Value(lhs.to_number().as_double() == rhs.to_number().as_double());

    if ((lhs.is_string() && rhs.is_boolean()) || (lhs.is_string() && rhs.is_boolean()))
        return Value(lhs.to_number().as_double() == rhs.to_number().as_double());

    return Value(false);
}

Value instance_of(Value lhs, Value rhs)
{
    if (!lhs.is_object() || !rhs.is_object())
        return Value(false);

    auto constructor_prototype_property = rhs.as_object().get("prototype");
    if (!constructor_prototype_property.has_value() || !constructor_prototype_property.value().is_object())
        return Value(false);

    return Value(lhs.as_object().has_prototype(&constructor_prototype_property.value().as_object()));
}

const LogStream& operator<<(const LogStream& stream, const Value& value)
{
    return stream << value.to_string();
}

}
