#include "Tree.hpp"

#include "pugixml.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace hem::pg
{
namespace
{
    const Tree& invalidTree() { static const Tree t; return t; }
}

bool Tree::hasProperty (std::string_view name) const noexcept
{
    for (auto& [k, v] : props) if (k == name) return true;
    return false;
}

std::string Tree::get (std::string_view name) const
{
    for (auto& [k, v] : props) if (k == name) return v;
    return {};
}

std::string Tree::get (std::string_view name, std::string_view fallback) const
{
    for (auto& [k, v] : props) if (k == name) return v;
    return std::string (fallback);
}

int Tree::getInt (std::string_view name, int fallback) const
{
    if (! hasProperty (name)) return fallback;
    const auto s = get (name);
    if (s == "true") return 1;
    if (s == "false") return 0;
    return (int) std::strtol (s.c_str(), nullptr, 10);   // "3", "3.0" and "-1" all read as whole numbers
}

double Tree::getDouble (std::string_view name, double fallback) const
{
    if (! hasProperty (name)) return fallback;
    const auto s = get (name);
    if (s == "true") return 1.0;
    if (s == "false") return 0.0;
    return std::strtod (s.c_str(), nullptr);
}

bool Tree::getBool (std::string_view name, bool fallback) const
{
    if (! hasProperty (name)) return fallback;
    const auto s = get (name);
    if (s == "true") return true;
    if (s == "false" || s.empty()) return false;
    return std::strtod (s.c_str(), nullptr) != 0.0;
}

Tree& Tree::set (std::string_view name, std::string_view value)
{
    for (auto& [k, v] : props) if (k == name) { v = std::string (value); return *this; }
    props.emplace_back (std::string (name), std::string (value));
    return *this;
}

Tree& Tree::set (std::string_view name, int value) { return set (name, std::string_view (std::to_string (value))); }
Tree& Tree::set (std::string_view name, double value) { return set (name, std::string_view (numberText (value))); }

void Tree::remove (std::string_view name)
{
    std::erase_if (props, [&] (auto& kv) { return kv.first == name; });
}

const Tree& Tree::child (std::string_view type) const noexcept
{
    for (auto& c : kids) if (c.nodeType == type) return c;
    return invalidTree();
}

Tree* Tree::findChild (std::string_view type) noexcept
{
    for (auto& c : kids) if (c.nodeType == type) return &c;
    return nullptr;
}

const Tree& Tree::childWith (std::string_view name, std::string_view value) const noexcept
{
    for (auto& c : kids) if (c.get (name) == value) return c;
    return invalidTree();
}

const Tree& Tree::childAt (std::size_t i) const noexcept { return i < kids.size() ? kids[i] : invalidTree(); }

std::string Tree::numberText (double v)
{
    if (v == (double) (long long) v && std::abs (v) < 1.0e15)
        return std::to_string ((long long) v);
    char buf[64];
    const auto r = std::to_chars (buf, buf + sizeof buf, v);   // shortest text that reads back exactly
    return std::string (buf, r.ptr);
}

//==============================================================================
namespace
{
    void toNode (const Tree& t, pugi::xml_node node)
    {
        for (auto& [k, v] : t.properties()) node.append_attribute (k.c_str()) = v.c_str();
        for (auto& c : t.children()) toNode (c, node.append_child (c.type().c_str()));
    }

    Tree fromNode (const pugi::xml_node& node)
    {
        Tree t (node.name());
        for (auto a : node.attributes()) t.set (a.name(), std::string_view (a.value()));
        for (auto c : node.children())
            if (c.type() == pugi::node_element) t.add (fromNode (c));
        return t;
    }
}

std::string Tree::toXml() const
{
    pugi::xml_document doc;
    auto decl = doc.append_child (pugi::node_declaration);
    decl.append_attribute ("version") = "1.0";
    decl.append_attribute ("encoding") = "UTF-8";
    toNode (*this, doc.append_child (nodeType.c_str()));
    std::ostringstream os;
    doc.save (os, "  ", pugi::format_default, pugi::encoding_utf8);
    return os.str();
}

std::optional<Tree> Tree::fromXml (std::string_view xml)
{
    pugi::xml_document doc;
    if (! doc.load_buffer (xml.data(), xml.size())) return std::nullopt;
    auto root = doc.document_element();
    if (! root) return std::nullopt;
    return fromNode (root);
}
} // namespace hem::pg
