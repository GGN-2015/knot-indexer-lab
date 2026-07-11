#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace cki::che_to_coord {

struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct OrderedPoint {
    std::int64_t atom_id = 0;
    Point3 position;
};

struct ParseOptions {
    bool prefer_atom_one_start = true;
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

using CoordinateLoop = std::vector<OrderedPoint>;
using CoordinateLink = std::vector<CoordinateLoop>;

CoordinateLoop parseCoordinateLoopText(const std::string& text,
                                       const ParseOptions& options = ParseOptions());
CoordinateLink parseCoordinateLinkText(const std::string& text,
                                       const ParseOptions& options = ParseOptions());
CoordinateLoop readCoordinateLoopFile(const std::filesystem::path& path,
                                      const ParseOptions& options = ParseOptions());
CoordinateLink readCoordinateLinkFile(const std::filesystem::path& path,
                                      const ParseOptions& options = ParseOptions());

std::vector<Point3> positionsOnly(const CoordinateLoop& loop);
std::vector<std::vector<Point3>> positionsOnly(const CoordinateLink& link);
std::string formatCoordinateLoop(const CoordinateLoop& loop);
std::string formatCoordinateLink(const CoordinateLink& link);
std::string formatLinkCoordinateText(const CoordinateLink& link);

}  // namespace cki::che_to_coord

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cki::che_to_coord {
namespace {

std::string displayPath(const std::filesystem::path& path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

struct SourceLine {
    std::size_t number = 0;
    std::string text;
};

struct AtomRecord {
    std::int64_t id = 0;
    Point3 position;
};

using Adjacency = std::unordered_map<std::int64_t, std::vector<std::int64_t>>;

struct EdgeKey {
    std::int64_t a = 0;
    std::int64_t b = 0;

    bool operator==(const EdgeKey& other) const {
        return a == other.a && b == other.b;
    }
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& key) const {
        const std::uint64_t a = static_cast<std::uint64_t>(key.a);
        const std::uint64_t b = static_cast<std::uint64_t>(key.b);
        return static_cast<std::size_t>((a * 11400714819323198485ull) ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2)));
    }
};

std::string trim(const std::string& input) {
    std::size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }
    std::size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(begin, end - begin);
}

std::string removeComment(const std::string& input) {
    const std::size_t pos = input.find('#');
    return trim(pos == std::string::npos ? input : input.substr(0, pos));
}

std::vector<std::string> splitTokens(const std::string& input) {
    std::istringstream stream(input);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string lowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::int64_t parseInteger(const std::string& token, std::size_t line_number, const char* field_name) {
    std::size_t used = 0;
    long long value = 0;
    try {
        value = std::stoll(token, &used, 10);
    } catch (const std::exception&) {
        throw ParseError("line " + std::to_string(line_number) + ": invalid integer in " +
                         field_name + ": " + token);
    }
    if (used != token.size()) {
        throw ParseError("line " + std::to_string(line_number) + ": invalid integer in " +
                         field_name + ": " + token);
    }
    return static_cast<std::int64_t>(value);
}

double parseDouble(const std::string& token, std::size_t line_number, const char* field_name) {
    std::size_t used = 0;
    double value = 0.0;
    try {
        value = std::stod(token, &used);
    } catch (const std::exception&) {
        throw ParseError("line " + std::to_string(line_number) + ": invalid coordinate in " +
                         field_name + ": " + token);
    }
    if (used != token.size() || !std::isfinite(value)) {
        throw ParseError("line " + std::to_string(line_number) + ": invalid coordinate in " +
                         field_name + ": " + token);
    }
    return value;
}

bool looksLikeSectionHeader(const std::string& raw_line) {
    const std::string line = removeComment(raw_line);
    if (line.empty()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(line.front());
    if (std::isdigit(first) || line.front() == '-' || line.front() == '+' || line.front() == '.') {
        return false;
    }
    return std::any_of(line.begin(), line.end(), [](char ch) {
        return std::isalpha(static_cast<unsigned char>(ch)) != 0;
    });
}

std::string sectionName(const std::string& raw_line) {
    if (!looksLikeSectionHeader(raw_line)) {
        return {};
    }
    const std::vector<std::string> tokens = splitTokens(removeComment(raw_line));
    return tokens.empty() ? std::string() : lowerAscii(tokens.front());
}

std::vector<SourceLine> linesFromText(const std::string& text) {
    std::vector<SourceLine> lines;
    std::istringstream stream(text);
    std::string line;
    std::size_t line_number = 1;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(SourceLine{line_number, line});
        ++line_number;
    }
    return lines;
}

std::map<std::string, std::vector<SourceLine>> extractSections(const std::vector<SourceLine>& lines) {
    std::map<std::string, std::vector<SourceLine>> sections;
    std::string current_section;

    for (const SourceLine& line : lines) {
        const std::string name = sectionName(line.text);
        if (!name.empty()) {
            current_section = name;
            sections.emplace(current_section, std::vector<SourceLine>());
            continue;
        }
        if (!current_section.empty()) {
            sections[current_section].push_back(line);
        }
    }

    return sections;
}

std::int64_t optionalHeaderCount(const std::vector<SourceLine>& lines, const std::string& name) {
    for (const SourceLine& line : lines) {
        const std::vector<std::string> tokens = splitTokens(removeComment(line.text));
        if (tokens.size() >= 2 && lowerAscii(tokens[1]) == name) {
            return parseInteger(tokens[0], line.number, name.c_str());
        }
    }
    return -1;
}

std::int64_t countDataRows(const std::vector<SourceLine>& lines) {
    return static_cast<std::int64_t>(std::count_if(lines.begin(), lines.end(), [](const SourceLine& line) {
        return !splitTokens(removeComment(line.text)).empty();
    }));
}

std::size_t coordinateFieldStart(const std::vector<std::string>& tokens, std::size_t line_number) {
    switch (tokens.size()) {
        case 4:
            return 1;  // id x y z
        case 5:
        case 8:
            return 2;  // atomic: id type x y z [ix iy iz]
        case 6:
        case 9:
            return 3;  // old/molecular: id mol type x y z [ix iy iz]
        case 7:
        case 10:
            return 4;  // full: id mol type q x y z [ix iy iz]
        default:
            throw ParseError("line " + std::to_string(line_number) +
                             ": unsupported atom row layout; expected id/x/y/z or common LAMMPS atom fields");
    }
}

std::unordered_map<std::int64_t, AtomRecord> parseAtoms(const std::vector<SourceLine>& lines) {
    std::unordered_map<std::int64_t, AtomRecord> atoms;

    for (const SourceLine& line : lines) {
        const std::vector<std::string> tokens = splitTokens(removeComment(line.text));
        if (tokens.empty()) {
            continue;
        }
        const std::int64_t id = parseInteger(tokens[0], line.number, "atom id");
        if (id <= 0) {
            throw ParseError("line " + std::to_string(line.number) + ": atom id must be positive");
        }

        const std::size_t coord = coordinateFieldStart(tokens, line.number);
        AtomRecord atom;
        atom.id = id;
        atom.position = Point3{
            parseDouble(tokens[coord], line.number, "x"),
            parseDouble(tokens[coord + 1], line.number, "y"),
            parseDouble(tokens[coord + 2], line.number, "z"),
        };

        if (!atoms.emplace(id, atom).second) {
            throw ParseError("line " + std::to_string(line.number) + ": duplicate atom id " +
                             std::to_string(id));
        }
    }

    return atoms;
}

Adjacency parseBonds(const std::vector<SourceLine>& lines,
                     const std::unordered_map<std::int64_t, AtomRecord>& atoms) {
    Adjacency adjacency;
    adjacency.reserve(atoms.size());
    std::unordered_set<EdgeKey, EdgeKeyHash> seen_edges;
    seen_edges.reserve(lines.size());

    for (const auto& entry : atoms) {
        adjacency.emplace(entry.first, std::vector<std::int64_t>());
    }

    for (const SourceLine& line : lines) {
        const std::vector<std::string> tokens = splitTokens(removeComment(line.text));
        if (tokens.empty()) {
            continue;
        }
        if (tokens.size() < 4) {
            throw ParseError("line " + std::to_string(line.number) +
                             ": bond row must be 'bond_id type atom1 atom2'");
        }

        const std::int64_t a = parseInteger(tokens[2], line.number, "bond atom1");
        const std::int64_t b = parseInteger(tokens[3], line.number, "bond atom2");
        if (a == b) {
            throw ParseError("line " + std::to_string(line.number) +
                             ": self-bond on atom " + std::to_string(a));
        }
        if (atoms.find(a) == atoms.end() || atoms.find(b) == atoms.end()) {
            throw ParseError("line " + std::to_string(line.number) +
                             ": bond endpoint references an unknown atom");
        }

        const auto edge = std::minmax(a, b);
        if (!seen_edges.emplace(EdgeKey{edge.first, edge.second}).second) {
            throw ParseError("line " + std::to_string(line.number) +
                             ": duplicate bond between atoms " + std::to_string(edge.first) +
                             " and " + std::to_string(edge.second));
        }
        adjacency[a].push_back(b);
        adjacency[b].push_back(a);
    }

    for (auto& entry : adjacency) {
        std::sort(entry.second.begin(), entry.second.end());
    }
    return adjacency;
}

std::vector<std::int64_t> traverseCycle(const Adjacency& adjacency,
                                        std::int64_t start,
                                        std::int64_t first_next,
                                        std::size_t expected_size) {
    std::vector<std::int64_t> order;
    order.reserve(expected_size);
    std::unordered_set<std::int64_t> visited;
    visited.reserve(expected_size);

    std::int64_t previous = start;
    std::int64_t current = start;
    std::int64_t next = first_next;
    while (true) {
        if (!visited.insert(current).second) {
            throw ParseError("cycle traversal repeated atom " + std::to_string(current) +
                             " before closing the loop");
        }
        order.push_back(current);

        if (next == start) {
            if (order.size() != expected_size) {
                throw ParseError("bonds contain a smaller closed cycle before all component atoms are visited");
            }
            break;
        }
        previous = current;
        current = next;
        const std::vector<std::int64_t>& neighbors = adjacency.at(current);
        next = neighbors[0] == previous ? neighbors[1] : neighbors[0];
    }

    return order;
}

std::vector<std::int64_t> orderCycleComponent(const Adjacency& adjacency,
                                             const std::vector<std::int64_t>& component_atoms,
                                             const ParseOptions& options) {
    if (component_atoms.size() < 3) {
        throw ParseError("a molecular link component must contain at least three atoms");
    }

    std::int64_t start = component_atoms.front();
    if (options.prefer_atom_one_start &&
        std::find(component_atoms.begin(), component_atoms.end(), 1) != component_atoms.end()) {
        start = 1;
    }

    const std::vector<std::int64_t>& neighbors = adjacency.at(start);
    std::vector<std::int64_t> first = traverseCycle(adjacency, start, neighbors[0], component_atoms.size());
    std::vector<std::int64_t> second = traverseCycle(adjacency, start, neighbors[1], component_atoms.size());
    return std::lexicographical_compare(second.begin(), second.end(), first.begin(), first.end()) ? second : first;
}

std::vector<std::vector<std::int64_t>> connectedCycleComponents(
    const std::unordered_map<std::int64_t, AtomRecord>& atoms,
    const Adjacency& adjacency) {
    if (atoms.empty()) {
        throw ParseError("no atoms were found");
    }

    for (const auto& entry : atoms) {
        const auto adj_it = adjacency.find(entry.first);
        const std::size_t degree = adj_it == adjacency.end() ? 0 : adj_it->second.size();
        if (degree != 2) {
            throw ParseError("atom " + std::to_string(entry.first) +
                             " has degree " + std::to_string(degree) +
                             "; expected degree 2 for every link component");
        }
    }

    std::vector<std::int64_t> atom_ids;
    atom_ids.reserve(atoms.size());
    for (const auto& entry : atoms) {
        atom_ids.push_back(entry.first);
    }
    std::sort(atom_ids.begin(), atom_ids.end());

    std::unordered_set<std::int64_t> visited;
    visited.reserve(atoms.size());
    std::vector<std::vector<std::int64_t>> components;

    for (std::int64_t root : atom_ids) {
        if (visited.find(root) != visited.end()) {
            continue;
        }

        std::vector<std::int64_t> stack{root};
        std::vector<std::int64_t> component;
        visited.insert(root);
        while (!stack.empty()) {
            const std::int64_t current = stack.back();
            stack.pop_back();
            component.push_back(current);
            for (std::int64_t neighbor : adjacency.at(current)) {
                if (visited.insert(neighbor).second) {
                    stack.push_back(neighbor);
                }
            }
        }
        std::sort(component.begin(), component.end());
        components.push_back(std::move(component));
    }

    std::sort(components.begin(), components.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.front() < rhs.front();
    });
    return components;
}

}  // namespace

inline CoordinateLink parseCoordinateLinkText(const std::string& text, const ParseOptions& options) {
    const std::vector<SourceLine> lines = linesFromText(text);
    const auto sections = extractSections(lines);

    const auto atom_section = sections.find("atoms");
    if (atom_section == sections.end()) {
        throw ParseError("missing Atoms section");
    }
    const auto bond_section = sections.find("bonds");
    if (bond_section == sections.end()) {
        throw ParseError("missing Bonds section");
    }

    const auto atoms = parseAtoms(atom_section->second);
    const Adjacency adjacency = parseBonds(bond_section->second, atoms);

    const std::int64_t expected_atoms = optionalHeaderCount(lines, "atoms");
    if (expected_atoms >= 0 && static_cast<std::int64_t>(atoms.size()) != expected_atoms) {
        throw ParseError("header declares " + std::to_string(expected_atoms) +
                         " atoms, but parsed " + std::to_string(atoms.size()));
    }
    const std::int64_t expected_bonds = optionalHeaderCount(lines, "bonds");
    if (expected_bonds >= 0 && countDataRows(bond_section->second) != expected_bonds) {
        throw ParseError("header declares " + std::to_string(expected_bonds) +
                         " bonds, but parsed " + std::to_string(countDataRows(bond_section->second)));
    }
    if (adjacency.size() != atoms.size()) {
        throw ParseError("internal adjacency size mismatch");
    }

    CoordinateLink link;
    const std::vector<std::vector<std::int64_t>> components = connectedCycleComponents(atoms, adjacency);
    link.reserve(components.size());
    for (const std::vector<std::int64_t>& component : components) {
        const std::vector<std::int64_t> order = orderCycleComponent(adjacency, component, options);
        CoordinateLoop loop;
        loop.reserve(order.size());
        for (std::int64_t atom_id : order) {
            const AtomRecord& atom = atoms.at(atom_id);
            loop.push_back(OrderedPoint{atom.id, atom.position});
        }
        link.push_back(std::move(loop));
    }
    return link;
}

inline CoordinateLoop parseCoordinateLoopText(const std::string& text, const ParseOptions& options) {
    CoordinateLink link = parseCoordinateLinkText(text, options);
    if (link.size() != 1) {
        throw ParseError("expected one connected cycle, found " + std::to_string(link.size()) +
                         "; use parseCoordinateLinkText for multi-component links");
    }
    return std::move(link.front());
}

inline CoordinateLoop readCoordinateLoopFile(const std::filesystem::path& path, const ParseOptions& options) {
    std::ifstream input(path);
    if (!input) {
        throw ParseError("cannot open molecule file: " + displayPath(path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseCoordinateLoopText(buffer.str(), options);
}

inline CoordinateLink readCoordinateLinkFile(const std::filesystem::path& path, const ParseOptions& options) {
    std::ifstream input(path);
    if (!input) {
        throw ParseError("cannot open molecule file: " + displayPath(path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseCoordinateLinkText(buffer.str(), options);
}

inline std::vector<Point3> positionsOnly(const CoordinateLoop& loop) {
    std::vector<Point3> points;
    points.reserve(loop.size());
    for (const OrderedPoint& point : loop) {
        points.push_back(point.position);
    }
    return points;
}

inline std::vector<std::vector<Point3>> positionsOnly(const CoordinateLink& link) {
    std::vector<std::vector<Point3>> components;
    components.reserve(link.size());
    for (const CoordinateLoop& loop : link) {
        components.push_back(positionsOnly(loop));
    }
    return components;
}

inline std::string formatCoordinateLoop(const CoordinateLoop& loop) {
    std::ostringstream output;
    output << std::setprecision(17);
    for (const OrderedPoint& point : loop) {
        output << point.atom_id << ' '
               << point.position.x << ' '
               << point.position.y << ' '
               << point.position.z << '\n';
    }
    return output.str();
}

inline std::string formatCoordinateLink(const CoordinateLink& link) {
    std::ostringstream output;
    output << std::setprecision(17);
    for (std::size_t component = 0; component < link.size(); ++component) {
        if (component != 0) {
            output << '\n';
        }
        output << formatCoordinateLoop(link[component]);
    }
    return output.str();
}

inline std::string formatLinkCoordinateText(const CoordinateLink& link) {
    std::ostringstream output;
    output << std::setprecision(17);
    output << link.size() << '\n';
    for (const CoordinateLoop& loop : link) {
        output << loop.size() << '\n';
        for (const OrderedPoint& point : loop) {
            output << point.position.x << ' '
                   << point.position.y << ' '
                   << point.position.z << '\n';
        }
    }
    return output.str();
}

}  // namespace cki::che_to_coord
