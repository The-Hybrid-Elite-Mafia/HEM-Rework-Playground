#pragma once
// =============================================================================
//  Tree — a small named node with text properties and child nodes, saved as XML.
//  Sessions, mixer state and plug-in settings are kept in Trees; the XML is the same layout the
//  0.10.0 app wrote (element = type, attribute = property, child elements = children), so its
//  .hemplay sessions and presets open here. Plain value type; message / worker threads only.
// =============================================================================
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hem::pg
{
class Tree
{
public:
    Tree() = default;
    explicit Tree (std::string type) : nodeType (std::move (type)) {}

    bool isValid() const noexcept { return ! nodeType.empty(); }
    const std::string& type() const noexcept { return nodeType; }
    bool hasType (std::string_view t) const noexcept { return nodeType == t; }

    //============================================================================== properties
    bool hasProperty (std::string_view name) const noexcept;
    /** Text of a property ("" if missing). */
    std::string get (std::string_view name) const;
    std::string get (std::string_view name, std::string_view fallback) const;
    int getInt (std::string_view name, int fallback = 0) const;
    double getDouble (std::string_view name, double fallback = 0.0) const;
    bool getBool (std::string_view name, bool fallback = false) const;

    Tree& set (std::string_view name, std::string_view value);
    Tree& set (std::string_view name, const char* value) { return set (name, std::string_view (value)); }
    Tree& set (std::string_view name, const std::string& value) { return set (name, std::string_view (value)); }
    Tree& set (std::string_view name, int value);
    Tree& set (std::string_view name, double value);
    Tree& set (std::string_view name, float value) { return set (name, (double) value); }
    Tree& set (std::string_view name, bool value) { return set (name, value ? 1 : 0); }
    void remove (std::string_view name);
    const std::vector<std::pair<std::string, std::string>>& properties() const noexcept { return props; }

    //============================================================================== children
    std::vector<Tree>& children() noexcept { return kids; }
    const std::vector<Tree>& children() const noexcept { return kids; }
    Tree& add (Tree child) { kids.push_back (std::move (child)); return kids.back(); }
    /** First child of that type, or an invalid Tree. */
    const Tree& child (std::string_view type) const noexcept;
    Tree* findChild (std::string_view type) noexcept;
    /** First child whose property `name` equals `value`, or an invalid Tree. */
    const Tree& childWith (std::string_view name, std::string_view value) const noexcept;
    const Tree& childAt (std::size_t i) const noexcept;

    bool operator== (const Tree&) const = default;

    //============================================================================== XML
    std::string toXml() const;
    static std::optional<Tree> fromXml (std::string_view xml);

    /** A number as text that reads back exactly ("0.25", "-14", "1.7999999523162842"). */
    static std::string numberText (double v);

private:
    std::string nodeType;
    std::vector<std::pair<std::string, std::string>> props;
    std::vector<Tree> kids;
};
} // namespace hem::pg
