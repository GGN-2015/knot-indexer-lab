#pragma once

#include "pd_code.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lab {
namespace name_pd_detail {

struct Slot {
    std::size_t crossing = 0;
    int position = 0;
};

struct NormalizeResult {
    hki::PDCode pd;
    std::unordered_map<int, int> oldToNew;
};

inline std::string trim(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

inline int pairedPosition(int position) {
    return (position + 2) % 4;
}

inline std::map<int, std::vector<Slot>> labelSlots(const hki::PDCode& pd) {
    std::map<int, std::vector<Slot>> slots;
    for (std::size_t crossing = 0; crossing < pd.size(); ++crossing) {
        for (int position = 0; position < 4; ++position) {
            slots[pd[crossing][position]].push_back(Slot{crossing, position});
        }
    }
    return slots;
}

inline std::map<int, std::vector<int>> pairedNeighbors(const hki::PDCode& pd) {
    std::map<int, std::vector<int>> neighbors;
    for (const auto& crossing : pd) {
        for (int position = 0; position < 4; ++position) {
            neighbors[crossing[position]].push_back(crossing[pairedPosition(position)]);
        }
    }
    return neighbors;
}

inline std::vector<int> walkCycle(const std::map<int, std::set<int>>& graph,
                                  int start,
                                  int firstNext,
                                  std::size_t expected) {
    std::vector<int> cycle{start};
    cycle.reserve(expected);
    int previous = start;
    int current = firstNext;
    while (current != start) {
        cycle.push_back(current);
        if (cycle.size() > expected) throw std::runtime_error("PD component cycle did not close");
        const auto& options = graph.at(current);
        int next = start;
        for (int candidate : options) {
            if (candidate != previous) {
                next = candidate;
                break;
            }
        }
        previous = current;
        current = next;
    }
    if (cycle.size() != expected) throw std::runtime_error("PD component cycle length mismatch");
    return cycle;
}

inline std::vector<int> canonicalCycle(const hki::PDCode& pd) {
    const auto neighbors = pairedNeighbors(pd);
    if (neighbors.empty()) return {};

    std::map<int, std::set<int>> graph;
    for (const auto& item : neighbors) {
        graph[item.first].insert(item.second.begin(), item.second.end());
        if (graph[item.first].size() != 2) {
            throw std::runtime_error("name-to-PD source is not a single knot component");
        }
    }

    const int start = graph.begin()->first;
    std::vector<std::vector<int>> candidates;
    for (int firstNext : graph.at(start)) {
        candidates.push_back(walkCycle(graph, start, firstNext, graph.size()));
    }
    if (candidates.size() != 2) throw std::runtime_error("expected two knot orientations");
    return std::lexicographical_compare(candidates[1].begin(), candidates[1].end(),
                                        candidates[0].begin(), candidates[0].end())
               ? candidates[1]
               : candidates[0];
}

inline NormalizeResult normalizePDCode(const hki::PDCode& input) {
    hki::validatePDCode(input);
    if (input.empty()) return NormalizeResult{input, {}};

    const std::vector<int> cycle = canonicalCycle(input);
    std::unordered_map<int, int> oldToNew;
    std::unordered_map<int, int> next;
    for (std::size_t index = 0; index < cycle.size(); ++index) {
        oldToNew[cycle[index]] = static_cast<int>(index + 1);
        next[static_cast<int>(index + 1)] = static_cast<int>((index + 1) % cycle.size() + 1);
    }

    hki::PDCode normalized = input;
    for (auto& crossing : normalized) {
        for (int& label : crossing) label = oldToNew.at(label);
        if (next.at(crossing[0]) == crossing[2]) continue;
        if (next.at(crossing[2]) == crossing[0]) {
            crossing = hki::Crossing{crossing[2], crossing[3], crossing[0], crossing[1]};
            continue;
        }
        throw std::runtime_error("crossing is inconsistent with knot orientation");
    }
    std::sort(normalized.begin(), normalized.end());
    hki::validatePDCode(normalized);
    return NormalizeResult{std::move(normalized), std::move(oldToNew)};
}

inline Slot endpointAfterLabel(const hki::PDCode& pd, int label) {
    const std::vector<int> cycle = canonicalCycle(pd);
    const auto found = std::find(cycle.begin(), cycle.end(), label);
    if (found == cycle.end()) throw std::runtime_error("connected-sum arc label is missing");
    const std::size_t index = static_cast<std::size_t>(found - cycle.begin());
    const int next = cycle[(index + 1) % cycle.size()];
    const int previous = cycle[(index + cycle.size() - 1) % cycle.size()];
    const auto slots = labelSlots(pd);
    const auto slotIt = slots.find(label);
    if (slotIt == slots.end() || slotIt->second.size() != 2) {
        throw std::runtime_error("connected-sum arc label must appear exactly twice");
    }
    if (next == previous) return slotIt->second[1];
    for (const Slot& slot : slotIt->second) {
        if (pd[slot.crossing][pairedPosition(slot.position)] == next) return slot;
    }
    throw std::runtime_error("could not locate oriented connected-sum endpoint");
}

inline hki::PDCode offsetPDCode(hki::PDCode pd, int offset) {
    for (auto& crossing : pd) {
        for (int& label : crossing) label += offset;
    }
    return pd;
}

inline hki::PDCode connectedSum(const hki::PDCode& leftInput, const hki::PDCode& rightInput) {
    if (leftInput.empty()) return normalizePDCode(rightInput).pd;
    if (rightInput.empty()) return normalizePDCode(leftInput).pd;

    const hki::PDCode left = normalizePDCode(leftInput).pd;
    hki::PDCode right = offsetPDCode(normalizePDCode(rightInput).pd,
                                     static_cast<int>(left.size() * 2));
    const int leftLabel = 1;
    const int rightLabel = static_cast<int>(left.size() * 2 + 1);
    const Slot leftAfter = endpointAfterLabel(left, leftLabel);
    const Slot rightAfter = endpointAfterLabel(right, rightLabel);

    hki::PDCode combined = left;
    combined.insert(combined.end(), right.begin(), right.end());
    combined[leftAfter.crossing][leftAfter.position] = rightLabel;
    combined[left.size() + rightAfter.crossing][rightAfter.position] = leftLabel;
    return normalizePDCode(combined).pd;
}

inline hki::PDCode mirrorPDCode(hki::PDCode pd) {
    for (auto& crossing : pd) std::swap(crossing[1], crossing[3]);
    return pd;
}

struct PrimeFactor {
    std::string baseName;
    bool mirror = false;
    int crossings = 0;
};

inline std::optional<PrimeFactor> parsePrimeFactor(const std::string& raw) {
    std::string value = trim(raw);
    PrimeFactor factor;
    if (!value.empty() && value[0] == 'm') {
        factor.mirror = true;
        value.erase(value.begin());
    }
    if (value.size() < 4 || value[0] != 'K') return std::nullopt;
    std::size_t position = 1;
    while (position < value.size() && std::isdigit(static_cast<unsigned char>(value[position]))) {
        factor.crossings = factor.crossings * 10 + (value[position] - '0');
        if (factor.crossings > 1000) return std::nullopt;
        ++position;
    }
    if (position == 1 || position >= value.size() || (value[position] != 'a' && value[position] != 'n')) {
        return std::nullopt;
    }
    const std::size_t indexStart = ++position;
    while (position < value.size() && std::isdigit(static_cast<unsigned char>(value[position]))) ++position;
    if (position != value.size() || position == indexStart) return std::nullopt;
    factor.baseName = std::move(value);
    return factor;
}

}  // namespace name_pd_detail

class NamePdLookup {
public:
    explicit NamePdLookup(std::filesystem::path tablePath) : tablePath_(std::move(tablePath)) {
        load();
    }

    std::optional<std::string> lookup(const std::string& canonicalName, std::string& error) const {
        if (canonicalName.empty()) {
            error = "knot name should not be empty.";
            return std::nullopt;
        }

        hki::PDCode result;
        std::size_t start = 0;
        while (start <= canonicalName.size()) {
            const std::size_t separator = canonicalName.find(',', start);
            const std::string rawFactor = canonicalName.substr(
                start, separator == std::string::npos ? std::string::npos : separator - start);
            const auto factor = name_pd_detail::parsePrimeFactor(rawFactor);
            if (!factor.has_value()) {
                error = "invalid prime knot name: " + rawFactor + ".";
                return std::nullopt;
            }

            hki::PDCode prime;
            if (factor->baseName != "K0a1") {
                const auto found = primePd_.find(factor->baseName);
                if (found == primePd_.end()) {
                    error = "prime knot " + factor->baseName + " is not present in " +
                            tablePath_.filename().string() + ".";
                    return std::nullopt;
                }
                prime = found->second;
                if (factor->mirror) prime = name_pd_detail::mirrorPDCode(std::move(prime));
            }
            result = name_pd_detail::connectedSum(result, prime);
            if (separator == std::string::npos) break;
            start = separator + 1;
        }

        error.clear();
        return hki::formatPDCodeList(result);
    }

    std::string statusMessage() const {
        return "loaded " + std::to_string(primePd_.size()) + " prime knot PD records from " +
               tablePath_.string();
    }

private:
    std::filesystem::path tablePath_;
    std::unordered_map<std::string, hki::PDCode> primePd_;

    void load() {
        std::ifstream input(tablePath_);
        if (!input) throw std::runtime_error("cannot open prime knot PD table: " + tablePath_.string());

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            line = name_pd_detail::trim(line);
            if (line.empty() || line[0] == '#') continue;
            const std::size_t separator = line.find('|');
            if (separator == std::string::npos) {
                throw std::runtime_error("bad prime knot PD table line " + std::to_string(lineNumber));
            }
            const std::string name = name_pd_detail::trim(line.substr(0, separator));
            const auto factor = name_pd_detail::parsePrimeFactor(name);
            if (!factor.has_value() || factor->mirror || factor->baseName != name) {
                throw std::runtime_error("bad prime knot name on PD table line " + std::to_string(lineNumber));
            }
            hki::PDCode pd = hki::parsePDCode(line.substr(separator + 1));
            if (pd.empty()) throw std::runtime_error("empty prime PD on table line " + std::to_string(lineNumber));
            if (pd.size() != static_cast<std::size_t>(factor->crossings)) {
                throw std::runtime_error("crossing count mismatch for prime knot " + name);
            }
            if (!primePd_.emplace(name, std::move(pd)).second) {
                throw std::runtime_error("duplicate prime knot PD record: " + name);
            }
        }
        if (primePd_.empty()) throw std::runtime_error("prime knot PD table is empty: " + tablePath_.string());
    }
};

}  // namespace lab
