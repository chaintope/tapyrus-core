// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <charconv>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdexcept>
#include <vector>
#include <limits>
#include <string>
#include <sstream>

#include "univalue.h"

namespace
{
static bool ParsePrechecks(const std::string& str)
{
    if (str.empty()) // No empty string allowed
        return false;
    if (str.size() >= 1 && (json_isspace(str[0]) || json_isspace(str[str.size()-1]))) // No padding allowed
        return false;
    if (str.size() != strlen(str.c_str())) // No embedded NUL characters allowed
        return false;
    return true;
}

bool ParseInt32(const std::string& str, int32_t *out)
{
    if (!ParsePrechecks(str))
        return false;
    int32_t n = 0;
    const char* data = str.data();
    auto result = std::from_chars(data, data + str.size(), n);
    if (out) *out = n;
    return result.ec == std::errc() && result.ptr == data + str.size();
}

bool ParseInt64(const std::string& str, int64_t *out)
{
    if (!ParsePrechecks(str))
        return false;
    int64_t n = 0;
    const char* data = str.data();
    auto result = std::from_chars(data, data + str.size(), n);
    if (out) *out = n;
    return result.ec == std::errc() && result.ptr == data + str.size();
}

bool ParseDouble(const std::string& str, double *out)
{
    if (!ParsePrechecks(str))
        return false;
    if (str.size() >= 2 && str[0] == '0' && str[1] == 'x') // No hexadecimal floats allowed
        return false;
    std::istringstream text(str);
    text.imbue(std::locale::classic());
    double result;
    text >> result;
    if(out) *out = result;
    return text.eof() && !text.fail();
}
}

const std::vector<std::string>& UniValue::getKeys() const
{
    if (typ != VOBJ)
        throw std::runtime_error("JSON value is not an object as expected");
    return keys;
}

const std::vector<UniValue>& UniValue::getValues() const
{
    if (typ != VOBJ && typ != VARR)
        throw std::runtime_error("JSON value is not an object or array as expected");
    return values;
}

bool UniValue::get_bool() const
{
    if (typ != VBOOL)
        throw std::runtime_error("JSON value is not a boolean as expected");
    return getBool();
}

const std::string& UniValue::get_str() const
{
    if (typ != VSTR)
        throw std::runtime_error("JSON value is not a string as expected");
    return getValStr();
}

int UniValue::get_int() const
{
    if (typ != VNUM)
        throw std::runtime_error("JSON value is not an integer as expected");
    int32_t retval;
    if (!ParseInt32(getValStr(), &retval))
        throw std::runtime_error("JSON integer out of range");
    return retval;
}

int64_t UniValue::get_int64() const
{
    if (typ != VNUM)
        throw std::runtime_error("JSON value is not an integer as expected");
    int64_t retval;
    if (!ParseInt64(getValStr(), &retval))
        throw std::runtime_error("JSON integer out of range");
    return retval;
}

double UniValue::get_real() const
{
    if (typ != VNUM)
        throw std::runtime_error("JSON value is not a number as expected");
    double retval;
    if (!ParseDouble(getValStr(), &retval))
        throw std::runtime_error("JSON double out of range");
    return retval;
}

const UniValue& UniValue::get_obj() const
{
    if (typ != VOBJ)
        throw std::runtime_error("JSON value is not an object as expected");
    return *this;
}

const UniValue& UniValue::get_array() const
{
    if (typ != VARR)
        throw std::runtime_error("JSON value is not an array as expected");
    return *this;
}

