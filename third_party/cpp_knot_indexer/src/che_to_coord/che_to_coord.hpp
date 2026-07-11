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

CoordinateLoop parseCoordinateLoopText(const std::string& text,
                                       const ParseOptions& options = ParseOptions());
CoordinateLoop readCoordinateLoopFile(const std::filesystem::path& path,
                                      const ParseOptions& options = ParseOptions());

std::vector<Point3> positionsOnly(const CoordinateLoop& loop);
std::string formatCoordinateLoop(const CoordinateLoop& loop);

}  // namespace cki::che_to_coord

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
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
    std::set<std::pair<std::int64_t, std::int64_t>> seen_edges;

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
        if (!seen_edges.emplace(edge.first, edge.second).second) {
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

std::vector<std::int64_t> orderSingleCycle(const std::unordered_map<std::int64_t, AtomRecord>& atoms,
                                           const Adjacency& adjacency,
                                           const ParseOptions& options) {
    if (atoms.empty()) {
        throw ParseError("no atoms were found");
    }
    if (atoms.size() < 3) {
        throw ParseError("a molecular knot loop must contain at least three atoms");
    }

    for (const auto& entry : atoms) {
        const auto adj_it = adjacency.find(entry.first);
        const std::size_t degree = adj_it == adjacency.end() ? 0 : adj_it->second.size();
        if (degree != 2) {
            throw ParseError("atom " + std::to_string(entry.first) +
                             " has degree " + std::to_string(degree) +
                             "; expected degree 2 for a single loop");
        }
    }

    std::int64_t start = std::numeric_limits<std::int64_t>::max();
    if (options.prefer_atom_one_start && atoms.find(1) != atoms.end()) {
        start = 1;
    } else {
        for (const auto& entry : atoms) {
            start = std::min(start, entry.first);
        }
    }

    std::vector<std::int64_t> order;
    order.reserve(atoms.size());
    std::unordered_set<std::int64_t> visited;

    std::int64_t previous = 0;
    std::int64_t current = start;
    while (true) {
        if (!visited.insert(current).second) {
            throw ParseError("cycle traversal repeated atom " + std::to_string(current) +
                             " before closing the loop");
        }
        order.push_back(current);

        const std::vector<std::int64_t>& neighbors = adjacency.at(current);
        std::int64_t next = neighbors[0] == previous ? neighbors[1] : neighbors[0];

        if (next == start) {
            if (order.size() != atoms.size()) {
                throw ParseError("bonds contain a smaller closed cycle before all atoms are visited");
            }
            break;
        }
        previous = current;
        current = next;
    }

    if (order.size() != atoms.size()) {
        throw ParseError("bond graph is not a connected single cycle");
    }
    return order;
}

}  // namespace

inline CoordinateLoop parseCoordinateLoopText(const std::string& text, const ParseOptions& options) {
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

    const std::vector<std::int64_t> order = orderSingleCycle(atoms, adjacency, options);
    CoordinateLoop loop;
    loop.reserve(order.size());
    for (std::int64_t atom_id : order) {
        const AtomRecord& atom = atoms.at(atom_id);
        loop.push_back(OrderedPoint{atom.id, atom.position});
    }
    return loop;
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

inline std::vector<Point3> positionsOnly(const CoordinateLoop& loop) {
    std::vector<Point3> points;
    points.reserve(loop.size());
    for (const OrderedPoint& point : loop) {
        points.push_back(point.position);
    }
    return points;
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

}  // namespace cki::che_to_coord
