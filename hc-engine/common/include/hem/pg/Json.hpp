#pragma once
// =============================================================================
//  Json — a small JSON value (null, bool, whole number, number, text, list, object) with a reader
//  and a writer. Objects keep their keys in the order they were added. Used for the library index,
//  the sound pack description and the Song Forge (ACE-Step) messages. Not for the audio thread.
// =============================================================================
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hem::pg
{
class Json
{
public:
    enum class Type { null, boolean, integer, number, string, array, object };

    Json() = default;
    Json (std::nullptr_t) {}
    Json (bool b) : kind (Type::boolean), integerValue (b ? 1 : 0) {}
    Json (int v) : kind (Type::integer), integerValue (v) {}
    Json (std::int64_t v) : kind (Type::integer), integerValue (v) {}
    Json (double v) : kind (Type::number), numberValue (v) {}
    Json (float v) : Json ((double) v) {}
    Json (const char* s) : kind (Type::string), text (s) {}
    Json (std::string s) : kind (Type::string), text (std::move (s)) {}
    Json (std::string_view s) : kind (Type::string), text (s) {}

    static Json array() { Json j; j.kind = Type::array; return j; }
    static Json object() { Json j; j.kind = Type::object; return j; }

    Type type() const noexcept { return kind; }
    bool isNull() const noexcept { return kind == Type::null; }
    bool isObject() const noexcept { return kind == Type::object; }
    bool isArray() const noexcept { return kind == Type::array; }
    bool isString() const noexcept { return kind == Type::string; }
    bool isNumber() const noexcept { return kind == Type::integer || kind == Type::number; }
    bool isBool() const noexcept { return kind == Type::boolean; }

    //============================================================================== reading values (forgiving, like 0.10.0)
    /** Text of a string; numbers and bools as text; "" for anything else. */
    std::string toString() const;
    double toDouble (double fallback = 0.0) const;
    std::int64_t toInt64 (std::int64_t fallback = 0) const;
    int toInt (int fallback = 0) const { return (int) toInt64 (fallback); }
    bool toBool (bool fallback = false) const;

    //============================================================================== objects
    bool has (std::string_view key) const noexcept;
    /** Member `key`, or a null value. */
    const Json& operator[] (std::string_view key) const noexcept;
    /** Sets (or adds, at the end) member `key`. Turns a null value into an object. */
    Json& set (std::string_view key, Json value);
    const std::vector<std::pair<std::string, Json>>& members() const noexcept { return objectItems; }

    //============================================================================== lists
    std::size_t size() const noexcept { return kind == Type::array ? arrayItems.size() : kind == Type::object ? objectItems.size() : 0; }
    const Json& operator[] (std::size_t i) const noexcept;
    const std::vector<Json>& items() const noexcept { return arrayItems; }
    Json& push (Json value);

    //============================================================================== text
    static std::optional<Json> parse (std::string_view text);
    /** pretty = indented over several lines (files); otherwise one line (messages). */
    std::string dump (bool pretty = true) const;

private:
    void write (std::string& out, bool pretty, int indent) const;

    Type kind = Type::null;
    std::int64_t integerValue = 0;
    double numberValue = 0.0;
    std::string text;
    std::vector<Json> arrayItems;
    std::vector<std::pair<std::string, Json>> objectItems;
};
} // namespace hem::pg
