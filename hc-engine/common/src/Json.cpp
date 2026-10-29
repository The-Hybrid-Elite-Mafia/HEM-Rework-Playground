#include "hem/pg/Json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hem::pg
{
namespace
{
    const Json& nullJson() { static const Json j; return j; }

    struct Reader
    {
        std::string_view s;
        std::size_t i = 0;
        int depth = 0;

        void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
        bool eat (char c) { skip(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
        bool word (std::string_view w) { if (s.substr (i, w.size()) == w) { i += w.size(); return true; } return false; }

        static void putUtf8 (std::string& out, std::uint32_t cp)
        {
            if (cp < 0x80) out += (char) cp;
            else if (cp < 0x800) { out += (char) (0xc0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3f)); }
            else if (cp < 0x10000) { out += (char) (0xe0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3f)); out += (char) (0x80 | (cp & 0x3f)); }
            else { out += (char) (0xf0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3f)); out += (char) (0x80 | ((cp >> 6) & 0x3f)); out += (char) (0x80 | (cp & 0x3f)); }
        }

        bool hex4 (std::uint32_t& v)
        {
            if (i + 4 > s.size()) return false;
            v = 0;
            for (int k = 0; k < 4; ++k)
            {
                const char c = s[i++];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= (std::uint32_t) (c - '0');
                else if (c >= 'a' && c <= 'f') v |= (std::uint32_t) (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (std::uint32_t) (c - 'A' + 10);
                else return false;
            }
            return true;
        }

        bool str (std::string& out)
        {
            if (! eat ('"')) return false;
            while (i < s.size())
            {
                const char c = s[i++];
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (i >= s.size()) return false;
                const char e = s[i++];
                switch (e)
                {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        std::uint32_t cp = 0;
                        if (! hex4 (cp)) return false;
                        if (cp >= 0xd800 && cp < 0xdc00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u')
                        {
                            i += 2;
                            std::uint32_t lo = 0;
                            if (! hex4 (lo)) return false;
                            cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                        }
                        putUtf8 (out, cp);
                        break;
                    }
                    default: return false;
                }
            }
            return false;
        }

        std::optional<Json> value()
        {
            skip();
            if (i >= s.size() || ++depth > 200) return std::nullopt;
            struct Leave { int& d; ~Leave() { --d; } } leave { depth };
            const char c = s[i];
            if (c == '{')
            {
                ++i;
                Json o = Json::object();
                if (eat ('}')) return o;
                do
                {
                    std::string key;
                    skip();
                    if (! str (key) || ! eat (':')) return std::nullopt;
                    auto v = value();
                    if (! v) return std::nullopt;
                    o.set (key, std::move (*v));
                } while (eat (','));
                if (! eat ('}')) return std::nullopt;
                return o;
            }
            if (c == '[')
            {
                ++i;
                Json a = Json::array();
                if (eat (']')) return a;
                do
                {
                    auto v = value();
                    if (! v) return std::nullopt;
                    a.push (std::move (*v));
                } while (eat (','));
                if (! eat (']')) return std::nullopt;
                return a;
            }
            if (c == '"') { std::string t; if (! str (t)) return std::nullopt; return Json (std::move (t)); }
            if (word ("true")) return Json (true);
            if (word ("false")) return Json (false);
            if (word ("null")) return Json();
            // number
            const std::size_t start = i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            bool isFloat = false;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+'))
            {
                if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') isFloat = true;
                ++i;
            }
            if (i == start) return std::nullopt;
            const std::string num (s.substr (start, i - start));
            if (! isFloat)
            {
                char* end = nullptr;
                const long long v = std::strtoll (num.c_str(), &end, 10);
                if (end != nullptr && *end == 0) return Json ((std::int64_t) v);
            }
            char* end = nullptr;
            const double d = std::strtod (num.c_str(), &end);
            if (end == nullptr || *end != 0) return std::nullopt;
            return Json (d);
        }
    };

    void escape (std::string& out, const std::string& t)
    {
        out += '"';
        for (unsigned char c : t)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); out += b; }
                    else out += (char) c;
            }
        }
        out += '"';
    }

    std::string numberText (double v)
    {
        if (! std::isfinite (v)) return "null";
        char b[64];
        const auto r = std::to_chars (b, b + sizeof b, v);
        std::string t (b, r.ptr);
        if (t.find_first_of (".eE") == std::string::npos) t += ".0";   // keep it a decimal number
        return t;
    }
}

std::string Json::toString() const
{
    switch (kind)
    {
        case Type::string: return text;
        case Type::integer: return std::to_string (integerValue);
        case Type::number: { auto t = numberText (numberValue); if (t.size() > 2 && t.ends_with (".0")) t.resize (t.size() - 2); return t; }
        case Type::boolean: return integerValue != 0 ? "true" : "false";
        default: return {};
    }
}

double Json::toDouble (double fallback) const
{
    switch (kind)
    {
        case Type::integer: case Type::boolean: return (double) integerValue;
        case Type::number: return numberValue;
        case Type::string: { char* end = nullptr; const double d = std::strtod (text.c_str(), &end); return end != text.c_str() ? d : 0.0; }
        default: return fallback;
    }
}

std::int64_t Json::toInt64 (std::int64_t fallback) const
{
    switch (kind)
    {
        case Type::integer: case Type::boolean: return integerValue;
        case Type::number: return (std::int64_t) numberValue;
        case Type::string: return (std::int64_t) std::strtoll (text.c_str(), nullptr, 10);
        default: return fallback;
    }
}

bool Json::toBool (bool fallback) const
{
    switch (kind)
    {
        case Type::boolean: case Type::integer: return integerValue != 0;
        case Type::number: return numberValue != 0.0;
        case Type::string: return text == "true" || std::strtoll (text.c_str(), nullptr, 10) != 0;
        default: return fallback;
    }
}

bool Json::has (std::string_view key) const noexcept
{
    for (auto& [k, v] : objectItems) if (k == key) return true;
    return false;
}

const Json& Json::operator[] (std::string_view key) const noexcept
{
    for (auto& [k, v] : objectItems) if (k == key) return v;
    return nullJson();
}

Json& Json::set (std::string_view key, Json value)
{
    if (kind == Type::null) kind = Type::object;
    for (auto& [k, v] : objectItems) if (k == key) { v = std::move (value); return v; }
    objectItems.emplace_back (std::string (key), std::move (value));
    return objectItems.back().second;
}

const Json& Json::operator[] (std::size_t i) const noexcept { return i < arrayItems.size() ? arrayItems[i] : nullJson(); }

Json& Json::push (Json value)
{
    if (kind == Type::null) kind = Type::array;
    arrayItems.push_back (std::move (value));
    return arrayItems.back();
}

std::optional<Json> Json::parse (std::string_view t)
{
    if (t.size() >= 3 && (unsigned char) t[0] == 0xef && (unsigned char) t[1] == 0xbb && (unsigned char) t[2] == 0xbf) t.remove_prefix (3);
    Reader r { t };
    auto v = r.value();
    if (! v) return std::nullopt;
    r.skip();
    if (r.i != t.size()) return std::nullopt;
    return v;
}

void Json::write (std::string& out, bool pretty, int indent) const
{
    auto newline = [&] (int level) { if (pretty) { out += '\n'; out.append ((size_t) level * 2, ' '); } };
    switch (kind)
    {
        case Type::null: out += "null"; break;
        case Type::boolean: out += integerValue != 0 ? "true" : "false"; break;
        case Type::integer: out += std::to_string (integerValue); break;
        case Type::number: out += numberText (numberValue); break;
        case Type::string: escape (out, text); break;
        case Type::array:
            out += '[';
            for (std::size_t k = 0; k < arrayItems.size(); ++k)
            {
                if (k > 0) out += pretty ? "," : ", ";
                newline (indent + 1);
                arrayItems[k].write (out, pretty, indent + 1);
            }
            if (! arrayItems.empty()) newline (indent);
            out += ']';
            break;
        case Type::object:
            out += '{';
            for (std::size_t k = 0; k < objectItems.size(); ++k)
            {
                if (k > 0) out += pretty ? "," : ", ";
                newline (indent + 1);
                escape (out, objectItems[k].first);
                out += ": ";
                objectItems[k].second.write (out, pretty, indent + 1);
            }
            if (! objectItems.empty()) newline (indent);
            out += '}';
            break;
    }
}

std::string Json::dump (bool pretty) const
{
    std::string out;
    write (out, pretty, 0);
    return out;
}
} // namespace hem::pg
