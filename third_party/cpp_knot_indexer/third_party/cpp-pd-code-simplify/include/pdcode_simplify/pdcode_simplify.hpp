#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <ostream>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef PDCODE_SIMPLIFY_API
#define PDCODE_SIMPLIFY_API
#endif

namespace pdcode_simplify {

struct Endpoint {
    int crossing = -1;
    int strand = -1;

    friend bool operator==(const Endpoint& lhs, const Endpoint& rhs) {
        return lhs.crossing == rhs.crossing && lhs.strand == rhs.strand;
    }

    friend bool operator!=(const Endpoint& lhs, const Endpoint& rhs) {
        return !(lhs == rhs);
    }
};

using Crossing = std::array<int, 4>;
using PDCode = std::vector<Crossing>;

enum class Direction {
    Left,
    Right
};

struct GreenCrossing {
    int from_face = -1;
    int to_face = -1;
    std::string strand_level;
};

struct SimplifierOptions {
    int max_paths = -1;
    int max_threads = -1;
    int timeout_seconds = -1;
    int quit_at_crossing = -1;
    long long bruteforce_budget = 200000;
    int reapr_retry_max = 3;
    bool has_timeout_deadline = false;
    std::chrono::steady_clock::time_point timeout_deadline{};
    bool ban_heuristic = false;
    bool enable_reapr = false;
    bool require_applicable = false;
    bool force_heuristic_parallel = false;
    bool verbose = false;
    std::function<void(const std::string&)> progress;
    std::function<void(int, const PDCode&)> step_pd_output;
    std::function<bool()> should_cancel;
};

struct LinkComponentSummary {
    std::vector<int> crossing_indices;
};

struct ComponentAnalysis {
    std::vector<LinkComponentSummary> components;
    std::size_t crossingless_components = 0;

    std::size_t components_with_crossings() const {
        return components.size();
    }

    std::size_t total_components() const {
        return components.size() + crossingless_components;
    }
};

struct RandomInflationOptions {
    int moves = 16;
    unsigned int seed = 1;
    int type_ii_percentage = 50;
};

struct RandomInflationResult {
    PDCode code;
    unsigned int seed = 1;
    int type_i_moves = 0;
    int type_ii_moves = 0;
};

struct PDSimplificationResult {
    PDCode code;
    std::size_t crossingless_components = 0;
    int reidemeister_i_moves = 0;
    int reidemeister_ii_moves = 0;
    int nugatory_crossing_moves = 0;
};

struct SimplificationResult {
    bool found = false;
    Direction direction = Direction::Left;
    std::string path_search_mode;
    std::vector<Endpoint> red_path;
    std::vector<int> green_path;
    std::vector<GreenCrossing> green_crossings;
    std::size_t tested_red_paths = 0;
    std::size_t tested_green_paths = 0;
    bool resource_limited = false;
    int crossing_reduction = 0;
    std::size_t resulting_crossings = std::numeric_limits<std::size_t>::max();
};

struct MidSimplificationApplyResult {
    PDCode code;
    std::size_t crossingless_components = 0;
};

struct ReductionResult {
    PDCode code;
    std::size_t crossingless_components = 0;
    int mid_simplification_rounds = 0;
    int heuristic_failover_rounds = 0;
    int reidemeister_i_moves = 0;
    int reidemeister_ii_moves = 0;
    int reidemeister_iii_moves = 0;
    int nugatory_crossing_moves = 0;
    std::size_t tested_red_paths = 0;
    std::size_t tested_green_paths = 0;
    std::string last_path_search_mode;
    bool reapr_used = false;
    bool reapr_rejected = false;
    int reapr_rounds = 0;
    int reapr_attempts = 0;
    std::string reapr_status;
    std::string reapr_warning;
    std::string alexander_determinant_before;
    std::string alexander_determinant_after;
    std::string reapr_invariants_before;
    std::string reapr_invariants_after;
    bool stopped_by_round_limit = false;
    bool stopped_by_crossing_limit = false;
    bool timed_out = false;
    bool resource_limited = false;
};

PDCODE_SIMPLIFY_API PDCode parse_pd_code(const std::string& text);
PDCODE_SIMPLIFY_API std::string format_pd_code(const PDCode& code);
PDCODE_SIMPLIFY_API std::string format_final_pd_code(const PDCode& code);
PDCODE_SIMPLIFY_API std::string format_endpoint(const Endpoint& endpoint);
PDCODE_SIMPLIFY_API std::string format_direction(Direction direction);

PDCODE_SIMPLIFY_API ComponentAnalysis analyze_components(
    const PDCode& code,
    std::size_t known_crossingless_components = 0);

PDCODE_SIMPLIFY_API ComponentAnalysis analyze_components_after_removing_crossings(
    const PDCode& code,
    const std::vector<int>& removed_crossings,
    std::size_t known_crossingless_components = 0);

PDCODE_SIMPLIFY_API std::size_t count_crossingless_components_after_removing_crossings(
    const PDCode& code,
    const std::vector<int>& removed_crossings,
    std::size_t known_crossingless_components = 0);

PDCODE_SIMPLIFY_API RandomInflationResult randomly_increase_crossings(
    const PDCode& code,
    const RandomInflationOptions& options = RandomInflationOptions{});

PDCODE_SIMPLIFY_API PDSimplificationResult simplify_pd_code(
    const PDCode& code,
    std::size_t known_crossingless_components = 0);

PDCODE_SIMPLIFY_API SimplificationResult find_simplification(
    const PDCode& code,
    const SimplifierOptions& options = SimplifierOptions{});

PDCODE_SIMPLIFY_API MidSimplificationApplyResult apply_simplification_witness(
    const PDCode& code,
    const SimplificationResult& result,
    std::size_t known_crossingless_components = 0);

PDCODE_SIMPLIFY_API ReductionResult reduce_pd_code(
    const PDCode& code,
    std::size_t known_crossingless_components = 0,
    const SimplifierOptions& options = SimplifierOptions{},
    int reduction_round = -1);

PDCODE_SIMPLIFY_API std::ostream& operator<<(std::ostream& out, const Endpoint& endpoint);

#ifndef PDCODE_SIMPLIFY_DECLARATIONS_ONLY

// Header-only implementation. All user-facing functions are inline so this
// header can be included by multiple translation units without a separate
// pdcode_simplify.cpp object file.
namespace {

constexpr int kBlockedWeight = 10000;
constexpr int kHeuristicBeamWidth = 8;
constexpr int kHeuristicMinStateBudget = 128;
constexpr int kHeuristicMaxStateBudget = 4096;
constexpr int kHeuristicMinPathBudget = 24;
constexpr int kHeuristicMaxPathBudget = 384;
constexpr int kHeuristicBestLookaheadBatches = 8;
constexpr int kEfficientPhaseTimeSliceSeconds = 180;
constexpr int kR3PrepassMaxDepth = 4;
constexpr int kR3PrepassMaxStates = 256;
constexpr int kR3PrepassTimeSliceSeconds = 15;
constexpr int kR3FailoverMaxDepth = 8;
constexpr int kR3FailoverMaxStates = 4096;
constexpr int kR3FailoverTimeSliceSeconds = 30;
constexpr int kMidSearchTimeSliceSeconds = 20;
constexpr int kNonMonotoneTimeSliceSeconds = 60;
constexpr int kNonMonotoneMaxRedLength = 80;
constexpr int kNonMonotoneMaxDepth = 72;
constexpr int kNonMonotoneBeamWidth = 32;
constexpr int kNonMonotoneMaxCandidatesPerState = 96;
constexpr int kNonMonotoneMaxCandidatesPerLength = 4;
constexpr int kNonMonotoneMaxRedScansPerLength = 48;
constexpr int kNonMonotoneMaxRedTestsPerNode = 64;
constexpr int kNonMonotoneExtraCrossings = 2;
constexpr int kNonMonotoneMaxTotalIncrease = 14;
constexpr int kNonMonotoneR3MovesPerState = 16;
constexpr int kNonMonotoneHeuristicStateBudget = 384;
constexpr int kNonMonotoneHeuristicPathBudget = 8;
constexpr std::size_t kNonMonotoneMaxGreenTestsPerState = 4096;
constexpr std::size_t kNonMonotoneMaxTotalGreenTests = 4000000;
constexpr std::size_t kPureFunctionCacheLimit = 4096;
constexpr const char* kReaprWarning =
    "WARNING: --reapr uses a deterministic internal reembedding/projection oracle "
    "guarded by component count, Alexander determinant, and finite-field "
    "Alexander root checks. The resulting PD code may still represent "
    "a different knot or link; verify additional invariants independently.";

int positive_mod(int value, int modulus) {
    int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

int endpoint_key(const Endpoint& endpoint) {
    return endpoint.crossing * 4 + endpoint.strand;
}

Endpoint endpoint_from_key(int key) {
    return Endpoint{key / 4, key % 4};
}

int max_label(const PDCode& code) {
    int result = -1;
    for (const Crossing& crossing : code) {
        for (int label : crossing) {
            result = std::max(result, label);
        }
    }
    return result;
}

std::uint64_t stable_hash_text(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename Cache>
void trim_pure_function_cache(Cache& cache) {
    if (cache.size() >= kPureFunctionCacheLimit) {
        cache.clear();
    }
}

template <typename Value>
struct SharedPureFunctionCache {
    std::mutex mutex;
    std::unordered_map<std::string, Value> values;
};

template <typename Value>
SharedPureFunctionCache<Value>& shared_pure_function_cache() {
    static SharedPureFunctionCache<Value>* cache = new SharedPureFunctionCache<Value>();
    return *cache;
}

std::string raw_code_cache_key(const PDCode& code) {
    return format_pd_code(code);
}

long long face_pair_key(int a, int b) {
    if (a > b) {
        std::swap(a, b);
    }
    return (static_cast<long long>(a) << 32) ^ static_cast<unsigned int>(b);
}

struct CrossingState {
    std::array<Endpoint, 4> adjacent{};
    bool directions[4][4]{};
    int sign = 0;
};

using LabelMap = std::unordered_map<int, std::vector<Endpoint>>;

PDCode canonical_output_code(const PDCode& code);

LabelMap build_label_map(const PDCode& code) {
    LabelMap labels;
    for (int c = 0; c < static_cast<int>(code.size()); ++c) {
        for (int i = 0; i < 4; ++i) {
            labels[code[c][i]].push_back(Endpoint{c, i});
        }
    }
    for (const auto& item : labels) {
        if (item.second.size() != 2) {
            std::ostringstream message;
            message << "PD label " << item.first << " appears " << item.second.size()
                    << " times; each label must appear exactly twice";
            throw std::invalid_argument(message.str());
        }
    }
    return labels;
}

Endpoint mate_endpoint(const PDCode& code, const LabelMap& labels, const Endpoint& endpoint) {
    const int label = code.at(endpoint.crossing).at(endpoint.strand);
    const std::vector<Endpoint>& endpoints = labels.at(label);
    if (endpoints[0] == endpoint) {
        return endpoints[1];
    }
    if (endpoints[1] == endpoint) {
        return endpoints[0];
    }
    throw std::logic_error("Endpoint is not present in its label map entry");
}

void replace_label(PDCode& code, int old_label, int new_label) {
    if (old_label == new_label) {
        return;
    }
    for (Crossing& crossing : code) {
        for (int& label : crossing) {
            if (label == old_label) {
                label = new_label;
            }
        }
    }
}

int unique_label_count(const Crossing& crossing) {
    return static_cast<int>(std::set<int>(crossing.begin(), crossing.end()).size());
}

std::vector<int> pd_value_set(const PDCode& code) {
    std::set<int> values;
    for (const Crossing& crossing : code) {
        values.insert(crossing.begin(), crossing.end());
    }
    return std::vector<int>(values.begin(), values.end());
}

bool contains_value(const std::vector<int>& values, int value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void add_undirected_vector_edge(std::map<int, std::vector<int>>& graph, int a, int b) {
    std::vector<int>& first = graph[a];
    if (std::find(first.begin(), first.end(), b) == first.end()) {
        first.push_back(b);
    }
    std::vector<int>& second = graph[b];
    if (std::find(second.begin(), second.end(), a) == second.end()) {
        second.push_back(a);
    }
}

void add_undirected_set_edge(std::map<int, std::set<int>>& graph, int a, int b) {
    graph[a].insert(b);
    graph[b].insert(a);
}

std::map<int, std::vector<int>> pd_adjacency_vector(const PDCode& code) {
    std::map<int, std::vector<int>> graph;
    for (const Crossing& crossing : code) {
        add_undirected_vector_edge(graph, crossing[0], crossing[2]);
        add_undirected_vector_edge(graph, crossing[1], crossing[3]);
    }
    return graph;
}

PDCode renumber_r1_order(PDCode code) {
    if (code.empty()) {
        return code;
    }

    const std::vector<int> values = pd_value_set(code);
    const std::map<int, std::vector<int>> graph = pd_adjacency_vector(code);
    std::vector<int> visit_order;
    for (int value : values) {
        if (contains_value(visit_order, value)) {
            continue;
        }
        if (graph.find(value) == graph.end()) {
            throw std::runtime_error("Invalid PD graph during R1 renumbering");
        }
        visit_order.push_back(value);
        while (true) {
            const int current = visit_order.back();
            std::vector<int> neighbors = graph.at(current);
            std::sort(neighbors.begin(), neighbors.end());
            bool advanced = false;
            for (int next : neighbors) {
                if (!contains_value(visit_order, next)) {
                    visit_order.push_back(next);
                    advanced = true;
                    break;
                }
            }
            if (!advanced) {
                break;
            }
        }
    }

    std::map<int, int> new_label;
    for (int i = 0; i < static_cast<int>(visit_order.size()); ++i) {
        new_label[visit_order[i]] = i;
    }
    for (Crossing& crossing : code) {
        for (int& label : crossing) {
            label = new_label.at(label);
        }
    }
    return code;
}

PDCode erase_r1_moves(
    PDCode code,
    std::size_t& crossingless_components,
    int& moves,
    bool canonicalize_result = true) {
    if (!code.empty()) {
        build_label_map(code);
    }

    while (true) {
        bool changed = false;
        for (int crossing_index = 0; crossing_index < static_cast<int>(code.size()); ++crossing_index) {
            if (unique_label_count(code[crossing_index]) > 3) {
                continue;
            }

            const Crossing crossing = code[crossing_index];
            const ComponentAnalysis after_removal =
                analyze_components_after_removing_crossings(
                    code, std::vector<int>{crossing_index}, crossingless_components);
            code.erase(code.begin() + crossing_index);

            std::vector<int> singles;
            for (int label : crossing) {
                if (std::count(crossing.begin(), crossing.end(), label) == 1) {
                    singles.push_back(label);
                }
            }
            if (singles.size() == 2) {
                replace_label(code, singles[0], singles[1]);
            }

            crossingless_components = after_removal.crossingless_components;
            if (canonicalize_result) {
                code = canonical_output_code(code);
            }
            ++moves;
            changed = true;
            break;
        }

        if (!changed) {
            break;
        }
    }

    const PDCode renumbered = renumber_r1_order(code);
    return canonicalize_result ? canonical_output_code(renumbered) : renumbered;
}

std::map<int, std::set<int>> base_pd_graph(const PDCode& code) {
    std::map<int, std::set<int>> graph;
    for (int i = 0; i < static_cast<int>(code.size()); ++i) {
        const int crossing_node = -i - 1;
        for (int label : code[i]) {
            add_undirected_set_edge(graph, label, crossing_node);
        }
    }
    return graph;
}

int graph_component_count(const PDCode& code) {
    const std::map<int, std::set<int>> graph = base_pd_graph(code);
    std::set<int> visited;
    int count = 0;
    for (const auto& item : graph) {
        const int start = item.first;
        if (visited.count(start) != 0) {
            continue;
        }
        ++count;
        std::vector<int> stack(1, start);
        visited.insert(start);
        while (!stack.empty()) {
            const int node = stack.back();
            stack.pop_back();
            const auto found = graph.find(node);
            if (found == graph.end()) {
                continue;
            }
            for (int next : found->second) {
                if (visited.insert(next).second) {
                    stack.push_back(next);
                }
            }
        }
    }
    return count;
}

bool is_nugatory_crossing(const PDCode& code, int crossing_index) {
    if (unique_label_count(code[crossing_index]) != 4) {
        throw std::runtime_error("Nugatory check requires an R1-free PD code");
    }
    PDCode without = code;
    without.erase(without.begin() + crossing_index);
    return graph_component_count(without) > graph_component_count(code);
}

int find_nugatory_crossing(const PDCode& code) {
    for (int i = 0; i < static_cast<int>(code.size()); ++i) {
        if (is_nugatory_crossing(code, i)) {
            return i;
        }
    }
    return -1;
}

void add_pre_next_edge(std::map<int, int>& previous, std::map<int, int>& next, int a, int b) {
    int previous_value = 0;
    int next_value = 0;
    if (std::abs(a - b) == 1) {
        previous_value = a < b ? a : b;
        next_value = a < b ? b : a;
    } else {
        previous_value = a < b ? b : a;
        next_value = a < b ? a : b;
    }
    previous[next_value] = previous_value;
    next[previous_value] = next_value;
}

std::pair<std::map<int, int>, std::map<int, int>> pre_next_maps(const PDCode& code) {
    if (!code.empty()) {
        build_label_map(code);
    }

    std::map<int, int> previous;
    std::map<int, int> next;
    for (const Crossing& crossing : code) {
        if (unique_label_count(crossing) > 2) {
            add_pre_next_edge(previous, next, crossing[0], crossing[2]);
            add_pre_next_edge(previous, next, crossing[1], crossing[3]);
        } else {
            std::vector<int> values(crossing.begin(), crossing.end());
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            if (values.size() != 2) {
                throw std::runtime_error("Invalid two-value crossing in pre/next maps");
            }
            previous[values[0]] = values[1];
            next[values[0]] = values[1];
            previous[values[1]] = values[0];
            next[values[1]] = values[0];
        }
    }

    for (int label : pd_value_set(code)) {
        if (previous.count(label) == 0) {
            if (next.count(label) == 0) {
                throw std::runtime_error("Broken PD pre/next map");
            }
            previous[label] = next[label];
        }
        if (next.count(label) == 0) {
            next[label] = previous[label];
        }
    }
    return std::make_pair(previous, next);
}

PDCode replace_arc_value(PDCode code, int from, int to) {
    replace_label(code, from, to);
    return code;
}

PDCode renumber_full_dfs(PDCode code) {
    if (code.empty()) {
        return code;
    }

    const std::vector<int> values = pd_value_set(code);
    std::map<int, std::set<int>> graph;
    for (const Crossing& crossing : code) {
        add_undirected_set_edge(graph, crossing[0], crossing[2]);
        add_undirected_set_edge(graph, crossing[1], crossing[3]);
    }

    std::set<int> visited;
    std::map<int, int> new_label;
    for (int start : values) {
        if (visited.count(start) != 0) {
            continue;
        }
        std::vector<int> stack(1, start);
        while (!stack.empty()) {
            const int value = stack.back();
            stack.pop_back();
            if (visited.count(value) != 0) {
                continue;
            }
            const auto found = graph.find(value);
            if (found == graph.end()) {
                throw std::runtime_error("Invalid PD graph during renumbering");
            }
            new_label[value] = static_cast<int>(visited.size());
            visited.insert(value);
            for (std::set<int>::const_reverse_iterator it = found->second.rbegin();
                 it != found->second.rend();
                 ++it) {
                if (visited.count(*it) == 0) {
                    stack.push_back(*it);
                }
            }
        }
    }

    if (new_label.size() != values.size()) {
        throw std::runtime_error("PD renumbering failed");
    }
    for (Crossing& crossing : code) {
        for (int& label : crossing) {
            label = new_label.at(label);
        }
    }
    return code;
}

PDCode erase_one_nugatory_crossing(
    PDCode code,
    int crossing_index,
    std::size_t& crossingless_components,
    int& moves,
    bool canonicalize_result = true) {
    if (unique_label_count(code[crossing_index]) != 4) {
        throw std::runtime_error("Nugatory erase requires an R1-free PD code");
    }

    const Crossing crossing = code[crossing_index];
    const int ax = crossing[0];
    const int bx = crossing[1];
    const int cx = crossing[2];
    const int dx = crossing[3];
    const std::map<int, int> next = pre_next_maps(code).second;

    std::vector<int> loop(1, ax);
    const std::size_t guard = pd_value_set(code).size() + 1;
    while (true) {
        const auto found = next.find(loop.back());
        if (found == next.end()) {
            throw std::runtime_error("Broken loop while erasing nugatory crossing");
        }
        const int next_label = found->second;
        loop.push_back(next_label);
        if (next_label == ax) {
            loop.pop_back();
            break;
        }
        if (loop.size() > guard) {
            throw std::runtime_error("Failed to close PD loop while erasing nugatory crossing");
        }
    }

    const std::set<int> loop_set(loop.begin(), loop.end());
    if (loop_set.count(ax) == 0 || loop_set.count(bx) == 0 ||
        loop_set.count(cx) == 0 || loop_set.count(dx) == 0) {
        throw std::runtime_error("Nugatory crossing arcs are not in one component");
    }

    const ComponentAnalysis after_removal =
        analyze_components_after_removing_crossings(
            code, std::vector<int>{crossing_index}, crossingless_components);

    code.erase(code.begin() + crossing_index);
    code = replace_arc_value(code, ax, cx);
    code = replace_arc_value(code, dx, bx);
    crossingless_components = after_removal.crossingless_components;
    ++moves;
    const PDCode renumbered = renumber_full_dfs(code);
    return canonicalize_result ? canonical_output_code(renumbered) : renumbered;
}

std::vector<int> endpoint_pairing(const PDCode& code) {
    const int endpoint_count = static_cast<int>(code.size() * 4);
    std::vector<int> pairing(endpoint_count, -1);
    const LabelMap labels = build_label_map(code);
    for (const auto& item : labels) {
        const Endpoint first = item.second[0];
        const Endpoint second = item.second[1];
        const int first_key = endpoint_key(first);
        const int second_key = endpoint_key(second);
        pairing[first_key] = second_key;
        pairing[second_key] = first_key;
    }
    return pairing;
}

void assign_pair(std::vector<int>& pairing, int first, int second) {
    if (first < 0 || second < 0 ||
        first >= static_cast<int>(pairing.size()) ||
        second >= static_cast<int>(pairing.size())) {
        throw std::out_of_range("Endpoint pairing assignment is out of range");
    }
    if (first == second) {
        throw std::runtime_error("Endpoint pairing assignment created a self-pair");
    }
    pairing[first] = second;
    pairing[second] = first;
}

PDCode code_from_endpoint_pairing(
    const std::vector<int>& pairing,
    int crossing_count,
    const std::set<int>& removed_crossings = std::set<int>()) {
    const int endpoint_count = crossing_count * 4;
    std::vector<char> active(endpoint_count, false);
    for (int crossing = 0; crossing < crossing_count; ++crossing) {
        if (removed_crossings.count(crossing) != 0) {
            continue;
        }
        for (int strand = 0; strand < 4; ++strand) {
            active[endpoint_key(Endpoint{crossing, strand})] = true;
        }
    }

    std::vector<int> label_by_endpoint(endpoint_count, -1);
    std::vector<char> seen(endpoint_count, false);
    int next_label = 0;
    for (int endpoint = 0; endpoint < endpoint_count; ++endpoint) {
        if (!active[endpoint] || seen[endpoint]) {
            continue;
        }
        const int mate = pairing.at(endpoint);
        if (mate < 0 || mate >= endpoint_count || !active[mate] || pairing.at(mate) != endpoint) {
            throw std::runtime_error("Endpoint rewrite produced a broken PD edge");
        }
        seen[endpoint] = true;
        seen[mate] = true;
        label_by_endpoint[endpoint] = next_label;
        label_by_endpoint[mate] = next_label;
        ++next_label;
    }

    PDCode output;
    output.reserve(static_cast<std::size_t>(crossing_count - removed_crossings.size()));
    for (int crossing = 0; crossing < crossing_count; ++crossing) {
        if (removed_crossings.count(crossing) != 0) {
            continue;
        }
        Crossing rewritten{};
        for (int strand = 0; strand < 4; ++strand) {
            const int key = endpoint_key(Endpoint{crossing, strand});
            if (label_by_endpoint[key] < 0) {
                throw std::runtime_error("Endpoint rewrite missed an active endpoint");
            }
            rewritten[strand] = label_by_endpoint[key];
        }
        output.push_back(rewritten);
    }
    return canonical_output_code(output);
}

std::vector<std::vector<int>> raw_faces_from_pd_code(const PDCode& code) {
    const LabelMap labels = build_label_map(code);
    const int endpoint_count = static_cast<int>(code.size() * 4);
    std::vector<char> present(endpoint_count, true);
    int remaining = endpoint_count;
    std::vector<std::vector<int>> faces;

    while (remaining > 0) {
        int first_key = -1;
        for (int key = endpoint_count - 1; key >= 0; --key) {
            if (present[key]) {
                first_key = key;
                break;
            }
        }
        if (first_key < 0) {
            break;
        }

        std::vector<int> face;
        Endpoint first = endpoint_from_key(first_key);
        Endpoint current = first;
        present[first_key] = false;
        --remaining;
        face.push_back(first_key);

        while (true) {
            const Endpoint next_corner{
                current.crossing,
                (current.strand + 1) % 4};
            const Endpoint next = mate_endpoint(code, labels, next_corner);
            if (next == first) {
                faces.push_back(std::move(face));
                break;
            }
            const int next_key = endpoint_key(next);
            if (present[next_key]) {
                present[next_key] = false;
                --remaining;
            }
            face.push_back(next_key);
            current = next;
        }
    }

    return faces;
}

struct Diagram {
    PDCode code;
    std::vector<CrossingState> crossings;
    std::vector<int> rotations;

    explicit Diagram(PDCode input)
        : code(std::move(input)), crossings(code.size()), rotations(code.size(), 0) {
        build_adjacency();
        auto starts = component_starts_from_pd();
        orient_crossings(starts);
    }

    Endpoint opposite(const Endpoint& endpoint) const {
        return crossings.at(endpoint.crossing).adjacent.at(endpoint.strand);
    }

    Endpoint next(const Endpoint& endpoint) const {
        return crossings.at(endpoint.crossing).adjacent.at((endpoint.strand + 2) % 4);
    }

    Endpoint next_corner(const Endpoint& endpoint) const {
        return crossings.at(endpoint.crossing).adjacent.at((endpoint.strand + 1) % 4);
    }

    Endpoint rotate_endpoint(const Endpoint& endpoint, int offset) const {
        return Endpoint{endpoint.crossing, positive_mod(endpoint.strand + offset, 4)};
    }

    int label_at(int crossing, int strand) const {
        return code.at(crossing).at(positive_mod(strand + rotations.at(crossing), 4));
    }

    std::vector<Endpoint> crossing_entries() const {
        std::vector<Endpoint> entries;
        entries.reserve(crossings.size() * 2);
        for (int c = 0; c < static_cast<int>(crossings.size()); ++c) {
            if (crossings[c].sign == -1) {
                entries.push_back(Endpoint{c, 0});
                entries.push_back(Endpoint{c, 1});
            } else if (crossings[c].sign == 1) {
                entries.push_back(Endpoint{c, 0});
                entries.push_back(Endpoint{c, 3});
            } else {
                throw std::logic_error("Crossing was not oriented");
            }
        }
        return entries;
    }

private:
    void build_adjacency() {
        std::map<int, std::vector<Endpoint>> gluings;
        for (int c = 0; c < static_cast<int>(code.size()); ++c) {
            for (int i = 0; i < 4; ++i) {
                gluings[code[c][i]].push_back(Endpoint{c, i});
            }
        }

        for (std::map<int, std::vector<Endpoint>>::const_iterator it = gluings.begin();
             it != gluings.end();
             ++it) {
            const int label = it->first;
            const std::vector<Endpoint>& endpoints = it->second;
            if (endpoints.size() != 2) {
                std::ostringstream message;
                message << "PD label " << label << " appears " << endpoints.size()
                        << " times; each label must appear exactly twice";
                throw std::invalid_argument(message.str());
            }
            const Endpoint a = endpoints[0];
            const Endpoint b = endpoints[1];
            crossings[a.crossing].adjacent[a.strand] = b;
            crossings[b.crossing].adjacent[b.strand] = a;
        }
    }

    std::vector<Endpoint> component_starts_from_pd() const {
        std::set<int> labels;
        std::map<int, std::vector<Endpoint>> gluings;
        for (int c = 0; c < static_cast<int>(code.size()); ++c) {
            for (int i = 0; i < 4; ++i) {
                labels.insert(code[c][i]);
                gluings[code[c][i]].push_back(Endpoint{c, i});
            }
        }

        std::vector<Endpoint> starts;
        while (!labels.empty()) {
            const int m = *labels.begin();
            labels.erase(labels.begin());
            const auto& gluing = gluings.at(m);
            const Endpoint first = gluing[0];
            const Endpoint second = gluing[1];

            Endpoint direction;
            int next_label = m;

            if (first.crossing == second.crossing) {
                std::set<int> crossing_labels(code[first.crossing].begin(), code[first.crossing].end());
                crossing_labels.erase(m);
                if (crossing_labels.empty()) {
                    throw std::invalid_argument("A PD self-loop crossing must have another label");
                }
                next_label = *crossing_labels.begin();
                direction = Endpoint{first.crossing, index_of_label(first.crossing, next_label)};
            } else {
                const int j1 = (first.strand + 2) % 4;
                const int j2 = (second.strand + 2) % 4;
                const int l1 = code[first.crossing][j1];
                const int l2 = code[second.crossing][j2];
                if (l1 < l2) {
                    next_label = l1;
                    direction = Endpoint{first.crossing, j1};
                } else if (l2 < l1) {
                    next_label = l2;
                    direction = Endpoint{second.crossing, j2};
                } else {
                    next_label = l1;
                    if (code[second.crossing][0] == l1 || code[first.crossing][0] == m) {
                        direction = Endpoint{first.crossing, j1};
                    } else {
                        direction = Endpoint{second.crossing, j2};
                    }
                }
            }

            starts.push_back(direction);
            while (next_label != m) {
                auto removed = labels.erase(next_label);
                if (removed == 0) {
                    throw std::invalid_argument("PD component traversal encountered a repeated label");
                }
                const auto& next_gluing = gluings.at(next_label);
                const int index = next_gluing[0] == direction ? 0 : (next_gluing[1] == direction ? 1 : -1);
                if (index == -1) {
                    throw std::invalid_argument("PD component traversal lost its current endpoint");
                }
                const Endpoint other = next_gluing[1 - index];
                direction = Endpoint{other.crossing, (other.strand + 2) % 4};
                next_label = code[direction.crossing][direction.strand];
            }
        }

        return starts;
    }

    int index_of_label(int crossing, int label) const {
        for (int i = 0; i < 4; ++i) {
            if (code[crossing][i] == label) {
                return i;
            }
        }
        throw std::logic_error("Label was not present at the requested crossing");
    }

    void make_tail(int crossing, int strand) {
        const int head = (strand + 2) % 4;
        if (crossings[crossing].directions[head][strand]) {
            throw std::invalid_argument("The same crossing strand was oriented twice");
        }
        crossings[crossing].directions[strand][head] = true;
    }

    void orient_crossings(std::vector<Endpoint> starts) {
        std::set<int> remaining;
        for (int c = 0; c < static_cast<int>(crossings.size()); ++c) {
            for (int i = 0; i < 4; ++i) {
                remaining.insert(endpoint_key(Endpoint{c, i}));
            }
        }

        while (!remaining.empty()) {
            Endpoint start;
            if (!starts.empty()) {
                start = starts.back();
                starts.pop_back();
            } else {
                start = endpoint_from_key(*remaining.begin());
            }

            Endpoint current = start;
            while (true) {
                const Endpoint other = crossings[current.crossing].adjacent[current.strand];
                make_tail(other.crossing, other.strand);
                remaining.erase(endpoint_key(current));
                remaining.erase(endpoint_key(other));
                current = Endpoint{other.crossing, (other.strand + 2) % 4};
                if (current == start) {
                    break;
                }
            }
        }

        for (int c = 0; c < static_cast<int>(crossings.size()); ++c) {
            orient_crossing(c);
        }
    }

    void orient_crossing(int crossing) {
        if (crossings[crossing].directions[2][0]) {
            rotate_crossing_180(crossing);
        }

        if (crossings[crossing].directions[3][1]) {
            crossings[crossing].sign = 1;
        } else if (crossings[crossing].directions[1][3]) {
            crossings[crossing].sign = -1;
        } else {
            throw std::invalid_argument("Could not determine crossing sign from PD orientation");
        }
    }

    void rotate_crossing_180(int crossing) {
        auto old_adjacent = crossings[crossing].adjacent;
        rotations[crossing] = positive_mod(rotations[crossing] + 2, 4);
        bool old_directions[4][4]{};
        for (int a = 0; a < 4; ++a) {
            for (int b = 0; b < 4; ++b) {
                old_directions[a][b] = crossings[crossing].directions[a][b];
                crossings[crossing].directions[a][b] = false;
            }
        }

        for (int i = 0; i < 4; ++i) {
            const Endpoint other = old_adjacent[(i + 2) % 4];
            if (other.crossing != crossing) {
                crossings[other.crossing].adjacent[other.strand] = Endpoint{crossing, i};
                crossings[crossing].adjacent[i] = other;
            } else {
                crossings[crossing].adjacent[i] = Endpoint{crossing, positive_mod(other.strand - 2, 4)};
            }
        }

        for (int a = 0; a < 4; ++a) {
            for (int b = 0; b < 4; ++b) {
                if (old_directions[a][b]) {
                    crossings[crossing].directions[(a + 2) % 4][(b + 2) % 4] = true;
                }
            }
        }
    }
};

struct GraphEdge {
    int u = -1;
    int v = -1;
    int interface_u = -1;
    int interface_v = -1;
    int weight = 1;
};

struct DualGraph {
    std::vector<int> edge_to_face;
    std::vector<int> face_assignment_order;
    std::vector<std::vector<int>> faces;
    std::vector<GraphEdge> edges;
    std::vector<std::vector<int>> adjacency;
    std::unordered_map<long long, int> edge_by_faces;

    explicit DualGraph(const Diagram& diagram) {
        build_faces(diagram);
        build_edges(diagram);
    }

    int edge_index(int a, int b) const {
        const auto found = edge_by_faces.find(face_pair_key(a, b));
        if (found == edge_by_faces.end()) {
            return -1;
        }
        return found->second;
    }

    const GraphEdge* edge(int a, int b) const {
        const int index = edge_index(a, b);
        if (index < 0) {
            return nullptr;
        }
        return &edges[index];
    }

    GraphEdge* mutable_edge(int a, int b) {
        const int index = edge_index(a, b);
        if (index < 0) {
            return nullptr;
        }
        return &edges[index];
    }

    int interface_for_face(const GraphEdge& edge, int face) const {
        if (edge.u == face) {
            return edge.interface_u;
        }
        if (edge.v == face) {
            return edge.interface_v;
        }
        throw std::logic_error("Face is not incident to the requested dual edge");
    }

private:
    void build_faces(const Diagram& diagram) {
        const int endpoint_count = static_cast<int>(diagram.crossings.size() * 4);
        edge_to_face.assign(endpoint_count, -1);
        std::vector<char> present(endpoint_count, true);
        int remaining = endpoint_count;

        while (remaining > 0) {
            int first_key = -1;
            for (int key = endpoint_count - 1; key >= 0; --key) {
                if (present[key]) {
                    first_key = key;
                    break;
                }
            }
            if (first_key == -1) {
                break;
            }

            const int face_index = static_cast<int>(faces.size());
            std::vector<int> face;
            Endpoint first = endpoint_from_key(first_key);
            Endpoint current = first;
            present[first_key] = false;
            --remaining;
            edge_to_face[first_key] = face_index;
            face_assignment_order.push_back(first_key);
            face.push_back(first_key);

            while (true) {
                Endpoint next = diagram.next_corner(current);
                if (next == first) {
                    faces.push_back(std::move(face));
                    break;
                }
                const int next_key = endpoint_key(next);
                edge_to_face[next_key] = face_index;
                face_assignment_order.push_back(next_key);
                if (present[next_key]) {
                    present[next_key] = false;
                    --remaining;
                }
                face.push_back(next_key);
                current = next;
            }
        }
    }

    void build_edges(const Diagram& diagram) {
        adjacency.assign(faces.size(), {});
        for (int key : face_assignment_order) {
            const Endpoint endpoint = endpoint_from_key(key);
            const Endpoint opposite = diagram.opposite(endpoint);
            const int opposite_key = endpoint_key(opposite);
            const int face = edge_to_face[key];
            const int neighbor = edge_to_face[opposite_key];
            if (face >= neighbor) {
                continue;
            }

            const long long pair_key = face_pair_key(face, neighbor);
            const auto found = edge_by_faces.find(pair_key);
            if (found == edge_by_faces.end()) {
                GraphEdge edge;
                edge.u = face;
                edge.v = neighbor;
                edge.interface_u = key;
                edge.interface_v = opposite_key;
                edge.weight = 1;
                const int edge_index = static_cast<int>(edges.size());
                edge_by_faces[pair_key] = edge_index;
                edges.push_back(edge);
                adjacency[face].push_back(edge_index);
                adjacency[neighbor].push_back(edge_index);
            } else {
                GraphEdge& edge = edges[found->second];
                if (edge.u == face) {
                    edge.interface_u = key;
                    edge.interface_v = opposite_key;
                } else {
                    edge.interface_u = opposite_key;
                    edge.interface_v = key;
                }
            }
        }
    }
};

struct ReidemeisterIIMove {
    bool found = false;
    int first_crossing = -1;
    int first_strand = -1;
    int second_crossing = -1;
    int second_strand = -1;
};

ReidemeisterIIMove find_reidemeister_ii_move(const PDCode& code) {
    if (code.size() < 2) {
        return ReidemeisterIIMove{};
    }
    const Diagram diagram(code);
    for (int crossing = 0; crossing < static_cast<int>(code.size()); ++crossing) {
        for (int strand = 0; strand < 4; ++strand) {
            const Endpoint first_neighbor = diagram.crossings[crossing].adjacent[strand];
            const Endpoint second_neighbor =
                diagram.crossings[crossing].adjacent[positive_mod(strand + 1, 4)];
            if (first_neighbor.crossing == crossing ||
                first_neighbor.crossing != second_neighbor.crossing) {
                continue;
            }
            if (positive_mod(first_neighbor.strand - 1, 4) != second_neighbor.strand) {
                continue;
            }
            if (positive_mod(strand + first_neighbor.strand, 2) != 0) {
                continue;
            }
            ReidemeisterIIMove move;
            move.found = true;
            move.first_crossing = crossing;
            move.first_strand = strand;
            move.second_crossing = first_neighbor.crossing;
            move.second_strand = first_neighbor.strand;
            return move;
        }
    }
    return ReidemeisterIIMove{};
}

PDCode erase_one_reidemeister_ii_move(
    const PDCode& code,
    const ReidemeisterIIMove& move,
    std::size_t& crossingless_components,
    int& moves) {
    if (!move.found) {
        throw std::invalid_argument("Cannot erase a missing Reidemeister II move");
    }

    const int crossing_count = static_cast<int>(code.size());
    std::vector<int> pairing = endpoint_pairing(code);
    const int a = move.first_crossing;
    const int b = move.second_crossing;
    const int strand = move.first_strand;
    const int other_strand = move.second_strand;

    const int w = pairing.at(endpoint_key(Endpoint{a, positive_mod(strand + 2, 4)}));
    const int x = pairing.at(endpoint_key(Endpoint{a, positive_mod(strand + 3, 4)}));
    const int y = pairing.at(endpoint_key(Endpoint{b, positive_mod(other_strand + 1, 4)}));
    const int z = pairing.at(endpoint_key(Endpoint{b, positive_mod(other_strand + 2, 4)}));
    assign_pair(pairing, w, z);
    assign_pair(pairing, x, y);

    const ComponentAnalysis after_removal =
        analyze_components_after_removing_crossings(
            code, std::vector<int>{a, b}, crossingless_components);
    crossingless_components = after_removal.crossingless_components;
    ++moves;

    std::set<int> removed;
    removed.insert(a);
    removed.insert(b);
    return code_from_endpoint_pairing(pairing, crossing_count, removed);
}

struct ReidemeisterIIIMove {
    std::array<Endpoint, 3> corners{};
};

std::vector<ReidemeisterIIIMove> possible_reidemeister_iii_moves(const PDCode& code) {
    if (code.size() < 3) {
        return {};
    }

    const std::string cache_key = raw_code_cache_key(code);
    auto& cache = shared_pure_function_cache<std::vector<ReidemeisterIIIMove>>();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto cached = cache.values.find(cache_key);
        if (cached != cache.values.end()) {
            return cached->second;
        }
    }

    std::vector<ReidemeisterIIIMove> moves;
    const Diagram diagram(code);
    const DualGraph graph(diagram);
    std::set<std::array<int, 6>> seen;
    for (const std::vector<int>& face : graph.faces) {
        if (face.size() != 3) {
            continue;
        }
        std::array<Endpoint, 3> corners{
            endpoint_from_key(face[0]),
            endpoint_from_key(face[1]),
            endpoint_from_key(face[2])};
        int parity_sum = 0;
        for (const Endpoint& endpoint : corners) {
            parity_sum += positive_mod(endpoint.strand, 2);
        }
        if (parity_sum != 1 && parity_sum != 2) {
            continue;
        }
        for (int rotate = 0; rotate < 3; ++rotate) {
            if (positive_mod(corners[1].strand, 2) == 0 &&
                positive_mod(corners[2].strand, 2) == 1) {
                break;
            }
            std::rotate(corners.begin(), corners.begin() + 1, corners.end());
        }
        if (positive_mod(corners[1].strand, 2) != 0 ||
            positive_mod(corners[2].strand, 2) != 1) {
            continue;
        }
        std::set<int> crossing_set;
        for (const Endpoint& endpoint : corners) {
            crossing_set.insert(endpoint.crossing);
        }
        if (crossing_set.size() != 3) {
            continue;
        }
        std::array<int, 6> key{};
        for (int i = 0; i < 3; ++i) {
            key[2 * i] = corners[i].crossing;
            key[2 * i + 1] = corners[i].strand;
        }
        if (!seen.insert(key).second) {
            continue;
        }
        ReidemeisterIIIMove move;
        move.corners = corners;
        moves.push_back(move);
    }
    std::sort(moves.begin(), moves.end(), [](const ReidemeisterIIIMove& lhs, const ReidemeisterIIIMove& rhs) {
        for (int i = 0; i < 3; ++i) {
            if (lhs.corners[i].crossing != rhs.corners[i].crossing) {
                return lhs.corners[i].crossing < rhs.corners[i].crossing;
            }
            if (lhs.corners[i].strand != rhs.corners[i].strand) {
                return lhs.corners[i].strand < rhs.corners[i].strand;
            }
        }
        return false;
    });
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        trim_pure_function_cache(cache.values);
        cache.values.emplace(cache_key, moves);
    }
    return moves;
}

PDCode apply_reidemeister_iii_move(const PDCode& code, const ReidemeisterIIIMove& move) {
    const int crossing_count = static_cast<int>(code.size());
    const int endpoint_count = crossing_count * 4;
    std::vector<int> pairing = endpoint_pairing(code);
    pairing.resize(static_cast<std::size_t>(endpoint_count + 12), -1);

    const Endpoint A = move.corners[0];
    const Endpoint B = move.corners[1];
    const Endpoint C = move.corners[2];
    const std::array<Endpoint, 6> old_border{
        Endpoint{C.crossing, positive_mod(C.strand - 1, 4)},
        Endpoint{C.crossing, positive_mod(C.strand - 2, 4)},
        Endpoint{A.crossing, positive_mod(A.strand - 1, 4)},
        Endpoint{A.crossing, positive_mod(A.strand - 2, 4)},
        Endpoint{B.crossing, positive_mod(B.strand - 1, 4)},
        Endpoint{B.crossing, positive_mod(B.strand - 2, 4)}};

    std::array<std::array<int, 2>, 6> temporary{};
    for (int i = 0; i < 6; ++i) {
        const int border = endpoint_key(old_border[i]);
        const int mate = pairing.at(border);
        const int first_temp = endpoint_count + 2 * i;
        const int second_temp = first_temp + 1;
        temporary[i] = std::array<int, 2>{first_temp, second_temp};
        assign_pair(pairing, first_temp, border);
        assign_pair(pairing, second_temp, mate);
    }

    const std::array<Endpoint, 6> new_border{
        Endpoint{A.crossing, positive_mod(A.strand, 4)},
        Endpoint{B.crossing, positive_mod(B.strand + 1, 4)},
        Endpoint{B.crossing, positive_mod(B.strand, 4)},
        Endpoint{C.crossing, positive_mod(C.strand + 1, 4)},
        Endpoint{C.crossing, positive_mod(C.strand, 4)},
        Endpoint{A.crossing, positive_mod(A.strand + 1, 4)}};
    for (int i = 0; i < 6; ++i) {
        assign_pair(pairing, endpoint_key(new_border[i]), temporary[i][0]);
    }

    assign_pair(
        pairing,
        endpoint_key(Endpoint{A.crossing, positive_mod(A.strand - 1, 4)}),
        endpoint_key(Endpoint{B.crossing, positive_mod(B.strand + 2, 4)}));
    assign_pair(
        pairing,
        endpoint_key(Endpoint{B.crossing, positive_mod(B.strand - 1, 4)}),
        endpoint_key(Endpoint{C.crossing, positive_mod(C.strand + 2, 4)}));
    assign_pair(
        pairing,
        endpoint_key(Endpoint{C.crossing, positive_mod(C.strand - 1, 4)}),
        endpoint_key(Endpoint{A.crossing, positive_mod(A.strand + 2, 4)}));

    for (const auto& temp : temporary) {
        const int first = temp[0];
        const int second = temp[1];
        const int first_mate = pairing.at(first);
        const int second_mate = pairing.at(second);
        assign_pair(pairing, first_mate, second_mate);
        pairing[first] = -1;
        pairing[second] = -1;
    }
    pairing.resize(static_cast<std::size_t>(endpoint_count));
    return code_from_endpoint_pairing(pairing, crossing_count);
}

enum class Level {
    Under,
    Over
};

std::string level_to_string(Level level) {
    return level == Level::Under ? "under" : "over";
}

Level opposite_level(Level level) {
    return level == Level::Under ? Level::Over : Level::Under;
}

std::vector<std::vector<Endpoint>> possible_red_lines(const Diagram& diagram) {
    std::vector<std::vector<Endpoint>> long_lines;
    std::vector<Endpoint> entries = diagram.crossing_entries();

    while (!entries.empty()) {
        std::vector<Endpoint> red_line;
        Endpoint endpoint = entries.back();
        entries.pop_back();
        red_line.push_back(endpoint);
        std::unordered_set<int> crossings;
        crossings.insert(endpoint.crossing);

        while (true) {
            endpoint = diagram.next(endpoint);
            red_line.push_back(endpoint);
            if (crossings.count(endpoint.crossing) != 0) {
                break;
            }
            crossings.insert(endpoint.crossing);
        }
        long_lines.push_back(std::move(red_line));
    }

    std::vector<std::vector<Endpoint>> candidates;
    for (const auto& line : long_lines) {
        if (line.size() < 3) {
            continue;
        }
        for (std::size_t i = 0; i < line.size() - 2; ++i) {
            candidates.emplace_back(line.begin(), line.end() - static_cast<std::ptrdiff_t>(i));
        }
    }
    return candidates;
}

std::vector<LinkComponentSummary> component_summaries(const Diagram& diagram) {
    std::set<int> remaining_entries;
    const std::vector<Endpoint> entries = diagram.crossing_entries();
    for (const Endpoint& endpoint : entries) {
        remaining_entries.insert(endpoint_key(endpoint));
    }

    std::vector<LinkComponentSummary> summaries;
    while (!remaining_entries.empty()) {
        Endpoint start = endpoint_from_key(*remaining_entries.rbegin());
        Endpoint current = start;
        std::set<int> crossing_set;

        while (true) {
            remaining_entries.erase(endpoint_key(current));
            crossing_set.insert(current.crossing);
            current = diagram.next(current);
            if (current == start) {
                break;
            }
        }

        LinkComponentSummary summary;
        summary.crossing_indices.assign(crossing_set.begin(), crossing_set.end());
        summaries.push_back(std::move(summary));
    }

    return summaries;
}

std::set<int> normalized_removed_crossings(const PDCode& code, const std::vector<int>& removed_crossings) {
    std::set<int> removed;
    for (int crossing : removed_crossings) {
        if (crossing < 0 || crossing >= static_cast<int>(code.size())) {
            std::ostringstream message;
            message << "Removed crossing index " << crossing << " is out of range";
            throw std::invalid_argument(message.str());
        }
        removed.insert(crossing);
    }
    return removed;
}

std::vector<int> heuristic_distances_to_target(
    const DualGraph& graph,
    int target,
    int cutoff,
    const SimplifierOptions& options);

void check_timeout(const SimplifierOptions& options);

bool visit_simple_paths_dfs(
    const DualGraph& graph,
    int current,
    int target,
    int cutoff,
    int current_weight,
    const std::vector<int>& distance,
    std::vector<char>& visited,
    std::vector<int>& current_path,
    const SimplifierOptions& options,
    const std::function<bool(const std::vector<int>&)>& visitor) {
    check_timeout(options);
    const int infinity = std::numeric_limits<int>::max() / 4;
    if (static_cast<int>(current_path.size()) - 1 >= cutoff) {
        return true;
    }
    if (current < 0 || current >= static_cast<int>(distance.size()) ||
        distance[current] == infinity || current_weight + distance[current] >= cutoff) {
        return true;
    }

    for (int edge_index : graph.adjacency[current]) {
        const GraphEdge& edge = graph.edges[edge_index];
        const int next = edge.u == current ? edge.v : edge.u;
        if (visited[next]) {
            continue;
        }
        const int next_weight = current_weight + edge.weight;
        if (next_weight >= cutoff) {
            continue;
        }
        if (next < 0 || next >= static_cast<int>(distance.size()) ||
            distance[next] == infinity || next_weight + distance[next] >= cutoff) {
            continue;
        }

        current_path.push_back(next);
        visited[next] = true;

        bool keep_going = true;
        if (next == target) {
            keep_going = visitor(current_path);
        } else {
            keep_going = visit_simple_paths_dfs(
                graph,
                next,
                target,
                cutoff,
                next_weight,
                distance,
                visited,
                current_path,
                options,
                visitor);
        }

        visited[next] = false;
        current_path.pop_back();
        if (!keep_going) {
            return false;
        }
    }
    return true;
}

bool visit_simple_paths(
    const DualGraph& graph,
    int source,
    int target,
    int cutoff,
    const SimplifierOptions& options,
    const std::function<bool(const std::vector<int>&)>& visitor) {
    check_timeout(options);
    if (source < 0 || target < 0 ||
        source >= static_cast<int>(graph.faces.size()) ||
        target >= static_cast<int>(graph.faces.size()) ||
        cutoff <= 0) {
        return true;
    }
    if (source == target) {
        return visitor(std::vector<int>{source});
    }

    std::vector<char> visited(graph.faces.size(), false);
    std::vector<int> current_path{source};
    const std::vector<int> distance = heuristic_distances_to_target(graph, target, cutoff, options);
    visited[source] = true;
    return visit_simple_paths_dfs(
        graph,
        source,
        target,
        cutoff,
        0,
        distance,
        visited,
        current_path,
        options,
        visitor);
}

std::vector<int> heuristic_distances_to_target(
    const DualGraph& graph,
    int target,
    int cutoff,
    const SimplifierOptions& options) {
    const int face_count = static_cast<int>(graph.faces.size());
    const int infinity = std::numeric_limits<int>::max() / 4;
    std::vector<int> distance(face_count, infinity);
    std::deque<int> queue;
    distance[target] = 0;
    queue.push_back(target);

    while (!queue.empty()) {
        check_timeout(options);
        const int current = queue.front();
        queue.pop_front();
        for (int edge_index : graph.adjacency[current]) {
            const GraphEdge& edge = graph.edges[edge_index];
            if (edge.weight >= cutoff) {
                continue;
            }
            const int next = edge.u == current ? edge.v : edge.u;
            if (distance[next] != infinity) {
                continue;
            }
            distance[next] = distance[current] + 1;
            queue.push_back(next);
        }
    }
    return distance;
}

struct HeuristicState {
    std::vector<int> path;
    std::vector<char> visited;
    int weight = 0;
    int branch_penalty = 0;
    int estimated_weight = 0;
    int estimated_length = 0;
    int serial = 0;
};

struct HeuristicStateWorse {
    bool operator()(const HeuristicState& lhs, const HeuristicState& rhs) const {
        if (lhs.estimated_weight != rhs.estimated_weight) {
            return lhs.estimated_weight > rhs.estimated_weight;
        }
        if (lhs.estimated_length != rhs.estimated_length) {
            return lhs.estimated_length > rhs.estimated_length;
        }
        if (lhs.branch_penalty != rhs.branch_penalty) {
            return lhs.branch_penalty > rhs.branch_penalty;
        }
        if (lhs.weight != rhs.weight) {
            return lhs.weight > rhs.weight;
        }
        if (lhs.path.size() != rhs.path.size()) {
            return lhs.path.size() > rhs.path.size();
        }
        return lhs.serial > rhs.serial;
    }
};

struct HeuristicStep {
    int next = -1;
    int edge_index = -1;
    int edge_weight = 0;
    int distance = 0;
    int degree_penalty = 0;
};

bool heuristic_step_less(const HeuristicStep& lhs, const HeuristicStep& rhs) {
    if (lhs.edge_weight != rhs.edge_weight) {
        return lhs.edge_weight < rhs.edge_weight;
    }
    if (lhs.distance != rhs.distance) {
        return lhs.distance < rhs.distance;
    }
    if (lhs.degree_penalty != rhs.degree_penalty) {
        return lhs.degree_penalty < rhs.degree_penalty;
    }
    if (lhs.next != rhs.next) {
        return lhs.next < rhs.next;
    }
    return lhs.edge_index < rhs.edge_index;
}

std::vector<std::vector<int>> collect_heuristic_paths(
    const DualGraph& graph,
    int source,
    int target,
    int cutoff,
    const SimplifierOptions& options) {
    check_timeout(options);
    std::vector<std::vector<int>> paths;
    const int face_count = static_cast<int>(graph.faces.size());
    if (source < 0 || target < 0 ||
        source >= face_count || target >= face_count || cutoff <= 0) {
        return paths;
    }
    if (source == target) {
        paths.push_back(std::vector<int>{source});
        return paths;
    }

    const std::vector<int> distance = heuristic_distances_to_target(graph, target, cutoff, options);
    const int infinity = std::numeric_limits<int>::max() / 4;
    if (distance[source] == infinity || distance[source] >= cutoff) {
        return paths;
    }

    const int state_budget = std::max(
        kHeuristicMinStateBudget,
        std::min(kHeuristicMaxStateBudget, face_count * std::max(1, cutoff) * 8));
    const int path_budget = std::max(
        kHeuristicMinPathBudget,
        std::min(kHeuristicMaxPathBudget, face_count * 2 + cutoff * 8));

    std::priority_queue<HeuristicState, std::vector<HeuristicState>, HeuristicStateWorse> queue;
    int serial = 0;
    HeuristicState initial;
    initial.path.push_back(source);
    initial.visited.assign(face_count, false);
    initial.visited[source] = true;
    initial.estimated_weight = distance[source];
    initial.estimated_length = distance[source];
    initial.serial = serial++;
    queue.push(initial);

    std::map<long long, int> popped_by_depth_face;
    int popped_states = 0;
    while (!queue.empty() && popped_states < state_budget &&
           static_cast<int>(paths.size()) < path_budget) {
        check_timeout(options);
        HeuristicState state = queue.top();
        queue.pop();
        ++popped_states;

        const int current = state.path.back();
        const int depth = static_cast<int>(state.path.size()) - 1;
        if (current == target) {
            if (state.weight < cutoff) {
                paths.push_back(state.path);
            }
            continue;
        }
        if (depth >= cutoff - 1) {
            continue;
        }

        const long long beam_key =
            static_cast<long long>(depth) * static_cast<long long>(face_count) + current;
        int& beam_count = popped_by_depth_face[beam_key];
        if (beam_count >= kHeuristicBeamWidth) {
            continue;
        }
        ++beam_count;

        std::vector<HeuristicStep> steps;
        for (int edge_index : graph.adjacency[current]) {
            const GraphEdge& edge = graph.edges[edge_index];
            const int next = edge.u == current ? edge.v : edge.u;
            if (state.visited[next]) {
                continue;
            }
            if (distance[next] == infinity) {
                continue;
            }
            const int new_weight = state.weight + edge.weight;
            if (new_weight >= cutoff) {
                continue;
            }
            const int new_depth = depth + 1;
            if (new_depth + distance[next] >= cutoff) {
                continue;
            }
            HeuristicStep step;
            step.next = next;
            step.edge_index = edge_index;
            step.edge_weight = edge.weight;
            step.distance = distance[next];
            step.degree_penalty = std::max(0, static_cast<int>(graph.adjacency[next].size()) - 2);
            steps.push_back(step);
        }
        std::sort(steps.begin(), steps.end(), heuristic_step_less);

        for (const HeuristicStep& step : steps) {
            const GraphEdge& edge = graph.edges[step.edge_index];
            HeuristicState next_state;
            next_state.path = state.path;
            next_state.path.push_back(step.next);
            next_state.visited = state.visited;
            next_state.visited[step.next] = true;
            next_state.weight = state.weight + edge.weight;
            next_state.branch_penalty = state.branch_penalty + step.degree_penalty;
            next_state.estimated_weight = next_state.weight + distance[step.next];
            next_state.estimated_length =
                static_cast<int>(next_state.path.size()) - 1 + distance[step.next];
            next_state.serial = serial++;
            queue.push(std::move(next_state));
        }
    }

    return paths;
}

std::vector<std::vector<int>> collect_limited_heuristic_paths(
    const DualGraph& graph,
    int source,
    int target,
    int cutoff,
    int state_budget,
    int path_budget,
    const SimplifierOptions& options) {
    check_timeout(options);
    std::vector<std::vector<int>> paths;
    const int face_count = static_cast<int>(graph.faces.size());
    if (source < 0 || target < 0 ||
        source >= face_count || target >= face_count || cutoff <= 0 ||
        state_budget <= 0 || path_budget <= 0) {
        return paths;
    }
    if (source == target) {
        paths.push_back(std::vector<int>{source});
        return paths;
    }

    const std::vector<int> distance = heuristic_distances_to_target(graph, target, cutoff, options);
    const int infinity = std::numeric_limits<int>::max() / 4;
    if (distance[source] == infinity || distance[source] >= cutoff) {
        return paths;
    }

    std::priority_queue<HeuristicState, std::vector<HeuristicState>, HeuristicStateWorse> queue;
    int serial = 0;
    HeuristicState initial;
    initial.path.push_back(source);
    initial.visited.assign(face_count, false);
    initial.visited[source] = true;
    initial.estimated_weight = distance[source];
    initial.estimated_length = distance[source];
    initial.serial = serial++;
    queue.push(initial);

    std::map<long long, int> popped_by_depth_face;
    int popped_states = 0;
    while (!queue.empty() && popped_states < state_budget &&
           static_cast<int>(paths.size()) < path_budget) {
        check_timeout(options);
        HeuristicState state = queue.top();
        queue.pop();
        ++popped_states;

        const int current = state.path.back();
        const int depth = static_cast<int>(state.path.size()) - 1;
        if (current == target) {
            if (state.weight < cutoff) {
                paths.push_back(state.path);
            }
            continue;
        }
        if (depth >= cutoff - 1) {
            continue;
        }

        const long long beam_key =
            static_cast<long long>(depth) * static_cast<long long>(face_count) + current;
        int& beam_count = popped_by_depth_face[beam_key];
        if (beam_count >= std::max(2, kHeuristicBeamWidth / 2)) {
            continue;
        }
        ++beam_count;

        std::vector<HeuristicStep> steps;
        for (int edge_index : graph.adjacency[current]) {
            const GraphEdge& edge = graph.edges[edge_index];
            const int next = edge.u == current ? edge.v : edge.u;
            if (state.visited[next]) {
                continue;
            }
            if (distance[next] == infinity) {
                continue;
            }
            const int new_weight = state.weight + edge.weight;
            if (new_weight >= cutoff) {
                continue;
            }
            const int new_depth = depth + 1;
            if (new_depth + distance[next] >= cutoff) {
                continue;
            }
            HeuristicStep step;
            step.next = next;
            step.edge_index = edge_index;
            step.edge_weight = edge.weight;
            step.distance = distance[next];
            step.degree_penalty = std::max(0, static_cast<int>(graph.adjacency[next].size()) - 2);
            steps.push_back(step);
        }
        std::sort(steps.begin(), steps.end(), heuristic_step_less);

        for (const HeuristicStep& step : steps) {
            const GraphEdge& edge = graph.edges[step.edge_index];
            HeuristicState next_state;
            next_state.path = state.path;
            next_state.path.push_back(step.next);
            next_state.visited = state.visited;
            next_state.visited[step.next] = true;
            next_state.weight = state.weight + edge.weight;
            next_state.branch_penalty = state.branch_penalty + step.degree_penalty;
            next_state.estimated_weight = next_state.weight + distance[step.next];
            next_state.estimated_length =
                static_cast<int>(next_state.path.size()) - 1 + distance[step.next];
            next_state.serial = serial++;
            queue.push(std::move(next_state));
        }
    }

    return paths;
}

bool contains_endpoint_key(const std::vector<int>& endpoints, int key) {
    return std::find(endpoints.begin(), endpoints.end(), key) != endpoints.end();
}

bool do_check(
    const Diagram& diagram,
    const DualGraph& graph,
    const std::vector<Endpoint>& red_path,
    const std::vector<int>& green_path,
    Direction direction,
    SimplificationResult& result,
    const SimplifierOptions& options) {
    check_timeout(options);
    std::vector<int> green_left_cross;
    green_left_cross.reserve(green_path.size());

    for (std::size_t i = 0; i + 1 < green_path.size(); ++i) {
        const int f1 = green_path[i];
        const int f2 = green_path[i + 1];
        const GraphEdge* edge = graph.edge(f1, f2);
        if (edge == nullptr) {
            return false;
        }
        const int face_for_interface = direction == Direction::Right ? f1 : f2;
        green_left_cross.push_back(graph.interface_for_face(*edge, face_for_interface));
    }

    std::unordered_set<int> red_boundary_crossings;
    std::deque<int> to_check;
    std::unordered_set<int> queued;
    std::unordered_map<int, Level> check_result;

    auto enqueue = [&](int key) {
        if (queued.insert(key).second) {
            to_check.push_back(key);
        }
    };

    auto erase_queued = [&](int key) {
        auto found = queued.find(key);
        if (found != queued.end()) {
            queued.erase(found);
            auto it = std::find(to_check.begin(), to_check.end(), key);
            if (it != to_check.end()) {
                to_check.erase(it);
            }
        }
    };

    for (std::size_t i = 0; i + 1 < red_path.size(); ++i) {
        const Endpoint red_endpoint = red_path[i];
        red_boundary_crossings.insert(red_endpoint.crossing);
        const int offset = direction == Direction::Right ? 3 : 1;
        const Endpoint cross_strand = diagram.rotate_endpoint(red_endpoint, offset);
        const int key = endpoint_key(cross_strand);
        enqueue(key);
        check_result[key] = (cross_strand.strand % 2 == 0) ? Level::Under : Level::Over;
    }

    std::vector<GreenCrossing> green_crossings;
    std::unordered_map<int, int> green_index;
    for (int i = 0; i < static_cast<int>(green_path.size()); ++i) {
        green_index[green_path[i]] = i;
    }

    bool good_path = true;
    while (!to_check.empty() && good_path) {
        check_timeout(options);
        const int start_key = to_check.back();
        to_check.pop_back();
        queued.erase(start_key);
        Endpoint cross_strand = endpoint_from_key(start_key);
        std::unordered_set<long long> trace_seen;

        while (true) {
            check_timeout(options);
            const int cross_key = endpoint_key(cross_strand);
            const Level current_level = check_result.at(cross_key);
            const long long trace_key =
                (static_cast<long long>(cross_key) << 1) ^
                static_cast<int>(current_level);
            if (!trace_seen.insert(trace_key).second) {
                good_path = false;
                break;
            }
            const Endpoint opposite = diagram.opposite(cross_strand);
            const int opposite_key = endpoint_key(opposite);
            const auto opposite_result = check_result.find(opposite_key);
            if (opposite_result != check_result.end() && opposite_result->second != current_level) {
                good_path = false;
                break;
            }

            if (contains_endpoint_key(green_left_cross, cross_key)) {
                const int f1 = graph.edge_to_face[cross_key];
                const int f2 = graph.edge_to_face[opposite_key];
                const auto f1_index = green_index.find(f1);
                const auto f2_index = green_index.find(f2);
                if (f1_index == green_index.end() || f2_index == green_index.end()) {
                    good_path = false;
                    break;
                }
                const bool forward = f1_index->second < f2_index->second;
                GreenCrossing green_crossing;
                green_crossing.from_face = forward ? f1 : f2;
                green_crossing.to_face = forward ? f2 : f1;
                green_crossing.strand_level = level_to_string(opposite_level(current_level));
                green_crossings.push_back(std::move(green_crossing));
                break;
            }

            check_result[opposite_key] = current_level;
            erase_queued(opposite_key);

            if (red_boundary_crossings.count(opposite.crossing) != 0) {
                break;
            }

            cross_strand = opposite;
            const Endpoint side1 = diagram.rotate_endpoint(cross_strand, 1);
            const Endpoint side2 = diagram.rotate_endpoint(cross_strand, 3);
            const int side1_key = endpoint_key(side1);
            const int side2_key = endpoint_key(side2);

            if (cross_strand.strand % 2 == 1 && current_level == Level::Under) {
                auto first = check_result.find(side1_key);
                auto second = check_result.find(side2_key);
                if ((first != check_result.end() && first->second == Level::Over) ||
                    (second != check_result.end() && second->second == Level::Over)) {
                    good_path = false;
                    break;
                }
                if (first == check_result.end()) {
                    check_result[side1_key] = Level::Under;
                    enqueue(side1_key);
                }
                if (second == check_result.end()) {
                    check_result[side2_key] = Level::Under;
                    enqueue(side2_key);
                }
            }

            if (cross_strand.strand % 2 == 0 && current_level == Level::Over) {
                auto first = check_result.find(side1_key);
                auto second = check_result.find(side2_key);
                if ((first != check_result.end() && first->second == Level::Under) ||
                    (second != check_result.end() && second->second == Level::Under)) {
                    good_path = false;
                    break;
                }
                if (first == check_result.end()) {
                    check_result[side1_key] = Level::Over;
                    enqueue(side1_key);
                }
                if (second == check_result.end()) {
                    check_result[side2_key] = Level::Over;
                    enqueue(side2_key);
                }
            }

            const Endpoint across_same_crossing = diagram.rotate_endpoint(cross_strand, 2);
            const int across_key = endpoint_key(across_same_crossing);
            check_result[across_key] = current_level;
            cross_strand = across_same_crossing;
        }
    }

    if (!good_path) {
        return false;
    }

    result.found = true;
    result.direction = direction;
    result.red_path = red_path;
    result.green_path = green_path;
    result.green_crossings = std::move(green_crossings);
    return true;
}

class DisjointSet {
public:
    int find(int value) {
        auto inserted = parent_.insert(std::make_pair(value, value));
        int parent = inserted.first->second;
        if (parent != value) {
            parent = find(parent);
            parent_[value] = parent;
        }
        return parent;
    }

    void unite(int first, int second) {
        int first_root = find(first);
        int second_root = find(second);
        if (first_root == second_root) {
            return;
        }
        if (second_root < first_root) {
            std::swap(first_root, second_root);
        }
        parent_[second_root] = first_root;
    }

private:
    std::map<int, int> parent_;
};

std::map<std::pair<int, int>, std::string> green_crossing_levels(
    const SimplificationResult& result) {
    std::set<std::pair<int, int>> path_edges;
    for (std::size_t i = 0; i + 1 < result.green_path.size(); ++i) {
        path_edges.insert(std::make_pair(result.green_path[i], result.green_path[i + 1]));
    }

    std::map<std::pair<int, int>, std::string> levels;
    for (const GreenCrossing& crossing : result.green_crossings) {
        const std::pair<int, int> edge_key =
            std::make_pair(crossing.from_face, crossing.to_face);
        if (path_edges.count(edge_key) == 0) {
            throw std::invalid_argument("Simplification witness has a green crossing outside the green path");
        }
        const auto inserted = levels.insert(std::make_pair(edge_key, crossing.strand_level));
        if (!inserted.second && inserted.first->second != crossing.strand_level) {
            throw std::invalid_argument("Simplification witness has conflicting green crossing levels");
        }
    }
    for (const auto& edge_key : path_edges) {
        if (levels.count(edge_key) == 0) {
            throw std::invalid_argument("Simplification witness is missing a green crossing level");
        }
    }
    return levels;
}

int crossing_graph_component_count(const PDCode& code) {
    const int crossing_count = static_cast<int>(code.size());
    if (crossing_count == 0) {
        return 0;
    }

    std::vector<int> parent(crossing_count);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int value) -> int {
        if (parent[value] != value) {
            parent[value] = find(parent[value]);
        }
        return parent[value];
    };
    auto unite = [&](int first, int second) {
        int first_root = find(first);
        int second_root = find(second);
        if (first_root == second_root) {
            return;
        }
        if (second_root < first_root) {
            std::swap(first_root, second_root);
        }
        parent[second_root] = first_root;
    };

    std::map<int, std::vector<int>> label_crossings;
    for (int crossing_index = 0; crossing_index < crossing_count; ++crossing_index) {
        for (int strand = 0; strand < 4; ++strand) {
            label_crossings[code[crossing_index][strand]].push_back(crossing_index);
        }
    }
    for (const auto& item : label_crossings) {
        if (item.second.size() != 2) {
            std::ostringstream message;
            message << "PD label " << item.first << " appears " << item.second.size()
                    << " times; each label must appear exactly twice";
            throw std::invalid_argument(message.str());
        }
        unite(item.second[0], item.second[1]);
    }

    std::set<int> roots;
    for (int crossing_index = 0; crossing_index < crossing_count; ++crossing_index) {
        roots.insert(find(crossing_index));
    }
    return static_cast<int>(roots.size());
}

bool is_planar_pd_code(const PDCode& code) {
    if (code.empty()) {
        return true;
    }
    Diagram diagram(code);
    DualGraph graph(diagram);
    const int vertices = static_cast<int>(code.size());
    const int edges = vertices * 2;
    const int faces = static_cast<int>(graph.faces.size());
    const int graph_components = crossing_graph_component_count(code);
    return vertices - edges + faces == 2 * graph_components;
}

}  // namespace

inline MidSimplificationApplyResult apply_simplification_witness(
    const PDCode& code,
    const SimplificationResult& result,
    std::size_t known_crossingless_components) {
    if (!result.found) {
        throw std::invalid_argument("Cannot apply a missing simplification witness");
    }
    if (result.red_path.size() < 2) {
        throw std::invalid_argument("Simplification witness red path is too short");
    }

    Diagram diagram(code);
    DualGraph graph(diagram);
    std::set<int> removed_crossings;
    std::map<int, int> red_entry_by_crossing;
    for (std::size_t i = 0; i + 1 < result.red_path.size(); ++i) {
        removed_crossings.insert(result.red_path[i].crossing);
        red_entry_by_crossing[result.red_path[i].crossing] = result.red_path[i].strand;
    }
    if (removed_crossings.size() != result.red_path.size() - 1) {
        throw std::invalid_argument("Simplification witness repeats a removed red crossing");
    }
    if (removed_crossings.count(result.red_path.back().crossing) != 0) {
        throw std::invalid_argument("Simplification witness ends inside the removed red arc");
    }

    const std::map<std::pair<int, int>, std::string> levels = green_crossing_levels(result);
    DisjointSet dsu;
    const int endpoint_count = static_cast<int>(code.size() * 4);
    const int new_crossing_count =
        result.green_path.empty() ? 0 : static_cast<int>(result.green_path.size()) - 1;
    const int new_base = endpoint_count;

    auto new_node = [&](int crossing_index, int strand) {
        return new_base + crossing_index * 4 + strand;
    };
    auto is_removed_node = [&](int node) {
        return node < endpoint_count && removed_crossings.count(node / 4) != 0;
    };
    auto is_removed_red_node = [&](int node) {
        if (!is_removed_node(node)) {
            return false;
        }
        const int crossing = node / 4;
        const int strand = node % 4;
        const int red_strand = red_entry_by_crossing.at(crossing);
        return strand == red_strand || strand == positive_mod(red_strand + 2, 4);
    };

    std::set<int> crossed_labels;
    struct CrossedEdge {
        int interface_from = -1;
        int interface_to = -1;
        std::string level;
    };
    std::vector<CrossedEdge> crossed_edges;
    crossed_edges.reserve(new_crossing_count);
    for (int i = 0; i < new_crossing_count; ++i) {
        const int from_face = result.green_path[i];
        const int to_face = result.green_path[i + 1];
        const GraphEdge* edge = graph.edge(from_face, to_face);
        if (edge == nullptr) {
            throw std::invalid_argument("Simplification witness green path crosses a missing dual edge");
        }
        const int interface_from = graph.interface_for_face(*edge, from_face);
        const int interface_to = graph.interface_for_face(*edge, to_face);
        if (is_removed_red_node(interface_from) || is_removed_red_node(interface_to)) {
            throw std::invalid_argument("Simplification witness crosses an edge removed with the red arc");
        }
        const int label = code[interface_from / 4][interface_from % 4];
        if (!crossed_labels.insert(label).second) {
            throw std::invalid_argument("Simplification witness crosses the same PD edge more than once");
        }
        const auto level = levels.find(std::make_pair(from_face, to_face));
        if (level == levels.end()) {
            throw std::invalid_argument("Simplification witness is missing a green crossing level");
        }
        CrossedEdge crossed;
        crossed.interface_from = interface_from;
        crossed.interface_to = interface_to;
        crossed.level = level->second;
        crossed_edges.push_back(std::move(crossed));
    }

    std::map<int, std::vector<int>> label_endpoints;
    for (int crossing_index = 0; crossing_index < static_cast<int>(code.size()); ++crossing_index) {
        for (int strand = 0; strand < 4; ++strand) {
            label_endpoints[code[crossing_index][strand]].push_back(crossing_index * 4 + strand);
        }
    }
    for (const auto& item : label_endpoints) {
        if (item.second.size() != 2) {
            std::ostringstream message;
            message << "PD label " << item.first << " appears " << item.second.size() << " times";
            throw std::invalid_argument(message.str());
        }
        if (crossed_labels.count(item.first) == 0) {
            dsu.unite(item.second[0], item.second[1]);
        }
    }

    for (const auto& item : red_entry_by_crossing) {
        const int crossing = item.first;
        const int strand = item.second;
        dsu.unite(crossing * 4 + positive_mod(strand + 1, 4),
                  crossing * 4 + positive_mod(strand + 3, 4));
    }

    int green_anchor = endpoint_key(result.red_path.front());
    for (int i = 0; i < static_cast<int>(crossed_edges.size()); ++i) {
        const CrossedEdge& crossed = crossed_edges[i];
        int existing_from_pos = -1;
        int existing_to_pos = -1;
        int green_in_pos = -1;
        int green_out_pos = -1;
        if (crossed.level == "over") {
            existing_from_pos = 0;
            existing_to_pos = 2;
            green_in_pos = 3;
            green_out_pos = 1;
        } else if (crossed.level == "under") {
            existing_from_pos = 1;
            green_in_pos = 0;
            green_out_pos = 2;
            existing_to_pos = 3;
        } else {
            throw std::invalid_argument("Unknown green crossing strand level: " + crossed.level);
        }

        dsu.unite(crossed.interface_from, new_node(i, existing_from_pos));
        dsu.unite(crossed.interface_to, new_node(i, existing_to_pos));
        dsu.unite(green_anchor, new_node(i, green_in_pos));
        green_anchor = new_node(i, green_out_pos);
    }
    dsu.unite(green_anchor, endpoint_key(result.red_path.back()));

    std::vector<int> active_nodes;
    active_nodes.reserve(endpoint_count + new_crossing_count * 4);
    for (int node = 0; node < endpoint_count; ++node) {
        if (!is_removed_node(node)) {
            active_nodes.push_back(node);
        }
    }
    for (int crossing = 0; crossing < new_crossing_count; ++crossing) {
        for (int strand = 0; strand < 4; ++strand) {
            active_nodes.push_back(new_node(crossing, strand));
        }
    }

    std::map<int, std::vector<int>> grouped;
    for (int node : active_nodes) {
        grouped[dsu.find(node)].push_back(node);
    }

    std::vector<std::vector<int>> groups;
    for (auto& item : grouped) {
        std::sort(item.second.begin(), item.second.end());
        groups.push_back(item.second);
    }
    std::sort(groups.begin(), groups.end(), [](const std::vector<int>& lhs, const std::vector<int>& rhs) {
        return lhs.front() < rhs.front();
    });

    std::map<int, int> label_by_node;
    int next_label = 0;
    for (const std::vector<int>& nodes : groups) {
        if (nodes.size() != 2) {
            std::ostringstream message;
            message << "Applied simplification produced a non-PD edge with "
                    << nodes.size() << " active endpoints";
            throw std::runtime_error(message.str());
        }
        for (int node : nodes) {
            label_by_node[node] = next_label;
        }
        ++next_label;
    }

    PDCode output;
    output.reserve(code.size() - removed_crossings.size() + static_cast<std::size_t>(new_crossing_count));
    for (int crossing_index = 0; crossing_index < static_cast<int>(code.size()); ++crossing_index) {
        if (removed_crossings.count(crossing_index) != 0) {
            continue;
        }
        Crossing crossing{};
        for (int strand = 0; strand < 4; ++strand) {
            crossing[strand] = label_by_node.at(crossing_index * 4 + strand);
        }
        output.push_back(crossing);
    }
    for (int crossing_index = 0; crossing_index < new_crossing_count; ++crossing_index) {
        Crossing crossing{};
        for (int strand = 0; strand < 4; ++strand) {
            crossing[strand] = label_by_node.at(new_node(crossing_index, strand));
        }
        output.push_back(crossing);
    }

    const std::size_t total_components =
        analyze_components(code, known_crossingless_components).total_components();
    output = renumber_full_dfs(output);
    if (!is_planar_pd_code(output)) {
        throw std::invalid_argument("Applied simplification produced a non-planar PD code");
    }
    const std::size_t crossing_components = analyze_components(output).components_with_crossings();

    MidSimplificationApplyResult applied;
    applied.code = std::move(output);
    applied.crossingless_components =
        total_components > crossing_components ? total_components - crossing_components : 0;
    return applied;
}

namespace {

void emit_progress(const SimplifierOptions& options, const std::string& message) {
    if (!options.verbose) {
        return;
    }
    if (options.progress) {
        options.progress(message);
    }
}

void emit_step_pd(const SimplifierOptions& options, int round, const PDCode& code) {
    if (options.step_pd_output) {
        options.step_pd_output(round, code);
    }
}

PDCode canonical_output_code(const PDCode& code) {
    const std::string cache_key = raw_code_cache_key(code);
    auto& cache = shared_pure_function_cache<PDCode>();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto cached = cache.values.find(cache_key);
        if (cached != cache.values.end()) {
            return cached->second;
        }
    }
    PDCode canonical = parse_pd_code(format_final_pd_code(code));
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        trim_pure_function_cache(cache.values);
        cache.values.emplace(cache_key, canonical);
    }
    return canonical;
}

std::string search_mode_for_options(const SimplifierOptions& options) {
    if (options.max_paths == -1 && !options.ban_heuristic) {
        return "heuristic";
    }
    if (options.max_paths == -1) {
        return "bruteforce";
    }
    return "bounded";
}

void validate_timeout_options(const SimplifierOptions& options) {
    if (options.timeout_seconds < -1 || options.timeout_seconds == 0) {
        throw std::invalid_argument("timeout must be -1 or a positive integer");
    }
    if (options.bruteforce_budget < -1 || options.bruteforce_budget == 0) {
        throw std::invalid_argument("bruteforce budget must be -1 or a positive integer");
    }
    if (options.quit_at_crossing < -1) {
        throw std::invalid_argument("quit_at_crossing must be -1 or a non-negative integer");
    }
    if (options.reapr_retry_max < 0) {
        throw std::invalid_argument("reapr retry max must be a non-negative integer");
    }
}

class TimeoutError : public std::runtime_error {
public:
    explicit TimeoutError(const std::string& message) : std::runtime_error(message) {}
};

SimplifierOptions with_timeout_deadline(SimplifierOptions options) {
    validate_timeout_options(options);
    if (options.timeout_seconds > 0 && !options.has_timeout_deadline) {
        options.has_timeout_deadline = true;
        options.timeout_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(options.timeout_seconds);
    }
    return options;
}

void check_timeout(const SimplifierOptions& options) {
    if (options.should_cancel && options.should_cancel()) {
        throw std::runtime_error("interrupted by Ctrl+C");
    }
    if (!options.has_timeout_deadline) {
        return;
    }
    if (std::chrono::steady_clock::now() >= options.timeout_deadline) {
        std::ostringstream message;
        message << "timeout after " << options.timeout_seconds << " seconds";
        throw TimeoutError(message.str());
    }
}

SimplifierOptions with_time_slice(SimplifierOptions options, int seconds) {
    if (seconds <= 0) {
        return options;
    }
    const auto soft_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    if (!options.has_timeout_deadline || soft_deadline < options.timeout_deadline) {
        options.has_timeout_deadline = true;
        options.timeout_deadline = soft_deadline;
        options.timeout_seconds = seconds;
    }
    return options;
}

bool timeout_expired(const SimplifierOptions& options) {
    return options.has_timeout_deadline &&
           std::chrono::steady_clock::now() >= options.timeout_deadline;
}

namespace alexander_determinant_guard {

const std::array<int, 3> kFingerprintPrimes = {1000003, 1000033, 1000037};

long long mod_pow(long long base, long long exponent, int modulus) {
    long long result = 1 % modulus;
    base %= modulus;
    while (exponent > 0) {
        if ((exponent & 1LL) != 0) {
            result = (result * base) % modulus;
        }
        base = (base * base) % modulus;
        exponent >>= 1LL;
    }
    return result;
}

int determinant_mod_prime(std::vector<std::vector<int>> matrix, int modulus) {
    const int n = static_cast<int>(matrix.size());
    if (n == 0) {
        return 1;
    }
    long long determinant = 1;
    for (int column = 0; column < n; ++column) {
        int pivot = column;
        while (pivot < n && matrix[pivot][column] == 0) {
            ++pivot;
        }
        if (pivot == n) {
            return 0;
        }
        if (pivot != column) {
            std::swap(matrix[pivot], matrix[column]);
            determinant = (modulus - determinant) % modulus;
        }
        const int pivot_value = matrix[column][column];
        determinant = (determinant * pivot_value) % modulus;
        const int inverse = static_cast<int>(mod_pow(pivot_value, modulus - 2, modulus));
        for (int row = column + 1; row < n; ++row) {
            if (matrix[row][column] == 0) {
                continue;
            }
            const int factor =
                static_cast<int>((static_cast<long long>(matrix[row][column]) * inverse) % modulus);
            for (int j = column; j < n; ++j) {
                const int value = static_cast<int>(
                    (matrix[row][j] -
                     static_cast<long long>(factor) * matrix[column][j]) %
                    modulus);
                matrix[row][j] = value < 0 ? value + modulus : value;
            }
        }
    }
    const int residue = static_cast<int>(determinant % modulus);
    return std::min(residue, (modulus - residue) % modulus);
}

std::vector<int> alexander_determinant_fingerprint(const PDCode& raw_code) {
    if (raw_code.empty()) {
        return std::vector<int>(kFingerprintPrimes.size(), 1);
    }

    const PDCode code = canonical_output_code(raw_code);
    std::map<int, int> label_to_index;
    for (const Crossing& crossing : code) {
        for (int label : crossing) {
            if (label_to_index.count(label) == 0) {
                const int next = static_cast<int>(label_to_index.size());
                label_to_index[label] = next;
            }
        }
    }

    DisjointSet dsu;
    for (const auto& item : label_to_index) {
        dsu.find(item.second);
    }
    for (const Crossing& crossing : code) {
        dsu.unite(label_to_index.at(crossing[1]), label_to_index.at(crossing[3]));
    }

    std::map<int, int> root_to_class;
    for (const auto& item : label_to_index) {
        const int root = dsu.find(item.second);
        if (root_to_class.count(root) == 0) {
            const int next = static_cast<int>(root_to_class.size());
            root_to_class[root] = next;
        }
    }

    const int rows = static_cast<int>(code.size());
    const int columns = static_cast<int>(root_to_class.size());
    if (rows <= 1 || columns <= 1) {
        return std::vector<int>(kFingerprintPrimes.size(), 1);
    }

    std::vector<int> fingerprint;
    fingerprint.reserve(kFingerprintPrimes.size());
    for (int modulus : kFingerprintPrimes) {
        std::vector<std::vector<int>> matrix(
            static_cast<std::size_t>(rows),
            std::vector<int>(static_cast<std::size_t>(columns), 0));
        for (int row = 0; row < rows; ++row) {
            const Crossing& crossing = code[static_cast<std::size_t>(row)];
            const int under_in = root_to_class.at(dsu.find(label_to_index.at(crossing[0])));
            const int over = root_to_class.at(dsu.find(label_to_index.at(crossing[1])));
            const int under_out = root_to_class.at(dsu.find(label_to_index.at(crossing[2])));
            matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(over)] =
                (matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(over)] + 2) % modulus;
            matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(under_in)] =
                (matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(under_in)] +
                 modulus - 1) %
                modulus;
            matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(under_out)] =
                (matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(under_out)] +
                 modulus - 1) %
                modulus;
        }

        const int minor_size = std::min(rows, columns) - 1;
        std::vector<std::vector<int>> minor(
            static_cast<std::size_t>(minor_size),
            std::vector<int>(static_cast<std::size_t>(minor_size), 0));
        for (int row = 0; row < minor_size; ++row) {
            for (int column = 0; column < minor_size; ++column) {
                minor[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                    matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
            }
        }
        fingerprint.push_back(determinant_mod_prime(std::move(minor), modulus));
    }
    return fingerprint;
}

std::string determinant_fingerprint_string(const std::vector<int>& fingerprint) {
    if (fingerprint.empty()) {
        return "";
    }
    const bool all_equal =
        std::all_of(fingerprint.begin(), fingerprint.end(), [&](int value) {
            return value == fingerprint.front();
        });
    if (all_equal) {
        return std::to_string(fingerprint.front());
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < fingerprint.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << fingerprint[i];
    }
    return out.str();
}

int single_small_determinant_value(const std::vector<int>& fingerprint) {
    if (fingerprint.empty()) {
        return -1;
    }
    for (int value : fingerprint) {
        if (value != fingerprint.front()) {
            return -1;
        }
    }
    return fingerprint.front();
}

}  // namespace alexander_determinant_guard

namespace reapr_invariant_guard {

struct ReaprInvariantProfile {
    std::size_t components = 0;
    std::vector<int> determinant_fingerprint;
    bool alexander_roots_evaluated = false;
    std::vector<int> alexander_roots_mod_11;
    std::vector<int> alexander_roots_mod_19;
    std::vector<int> alexander_roots_mod_31;
};

bool same_basic_profile(const ReaprInvariantProfile& left, const ReaprInvariantProfile& right) {
    return left.components == right.components &&
           left.determinant_fingerprint == right.determinant_fingerprint;
}

bool same_profile(const ReaprInvariantProfile& left, const ReaprInvariantProfile& right) {
    return same_basic_profile(left, right) &&
           left.alexander_roots_evaluated &&
           right.alexander_roots_evaluated &&
           left.alexander_roots_mod_11 == right.alexander_roots_mod_11 &&
           left.alexander_roots_mod_19 == right.alexander_roots_mod_19 &&
           left.alexander_roots_mod_31 == right.alexander_roots_mod_31;
}

std::string int_vector_string(const std::vector<int>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << values[i];
    }
    out << ']';
    return out.str();
}

std::vector<int> alexander_roots_mod_prime(
    const PDCode& raw_code,
    int modulus,
    const SimplifierOptions* options) {
    if (raw_code.empty()) {
        return {};
    }

    const PDCode code = canonical_output_code(raw_code);
    const Diagram diagram(code);
    std::map<int, int> label_to_index;
    for (int crossing = 0; crossing < static_cast<int>(code.size()); ++crossing) {
        for (int strand = 0; strand < 4; ++strand) {
            const int label = diagram.label_at(crossing, strand);
            if (label_to_index.count(label) == 0) {
                const int next = static_cast<int>(label_to_index.size());
                label_to_index[label] = next;
            }
        }
    }

    DisjointSet dsu;
    for (const auto& item : label_to_index) {
        dsu.find(item.second);
    }
    for (int crossing = 0; crossing < static_cast<int>(code.size()); ++crossing) {
        dsu.unite(label_to_index.at(diagram.label_at(crossing, 1)),
                  label_to_index.at(diagram.label_at(crossing, 3)));
    }

    std::map<int, int> root_to_class;
    for (const auto& item : label_to_index) {
        const int root = dsu.find(item.second);
        if (root_to_class.count(root) == 0) {
            const int next = static_cast<int>(root_to_class.size());
            root_to_class[root] = next;
        }
    }

    const int rows = static_cast<int>(code.size());
    const int columns = static_cast<int>(root_to_class.size());
    if (rows <= 1 || columns <= 1) {
        return {};
    }
    const int minor_size = std::min(rows, columns) - 1;

    struct RowSpec {
        int under_in = 0;
        int over = 0;
        int under_out = 0;
        int sign = 1;
    };
    std::vector<RowSpec> row_specs;
    row_specs.reserve(static_cast<std::size_t>(minor_size));
    for (int row = 0; row < minor_size; ++row) {
        RowSpec spec;
        spec.under_in = root_to_class.at(dsu.find(label_to_index.at(diagram.label_at(row, 0))));
        spec.over = root_to_class.at(dsu.find(label_to_index.at(diagram.label_at(row, 1))));
        spec.under_out = root_to_class.at(dsu.find(label_to_index.at(diagram.label_at(row, 2))));
        spec.sign = diagram.crossings[static_cast<std::size_t>(row)].sign;
        row_specs.push_back(spec);
    }

    std::vector<int> roots;
    for (int t = 1; t < modulus; ++t) {
        if (options != nullptr) {
            check_timeout(*options);
        }
        std::vector<std::vector<int>> minor(
            static_cast<std::size_t>(minor_size),
            std::vector<int>(static_cast<std::size_t>(minor_size), 0));
        for (int row = 0; row < minor_size; ++row) {
            const RowSpec& spec = row_specs[static_cast<std::size_t>(row)];
            const int one_minus_t = positive_mod(1 - t, modulus);
            auto add_value = [&](int column, int value) {
                if (column >= minor_size) {
                    return;
                }
                int& entry =
                    minor[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                entry = positive_mod(entry + value, modulus);
            };
            if (spec.sign == 1) {
                add_value(spec.over, one_minus_t);
                add_value(spec.under_in, t);
                add_value(spec.under_out, -1);
            } else {
                add_value(spec.over, one_minus_t);
                add_value(spec.under_in, -1);
                add_value(spec.under_out, t);
            }
        }
        if (alexander_determinant_guard::determinant_mod_prime(std::move(minor), modulus) == 0) {
            roots.push_back(t);
        }
    }
    return roots;
}

ReaprInvariantProfile profile_for_code(
    const PDCode& code,
    std::size_t crossingless_components,
    const SimplifierOptions* options,
    bool include_alexander_roots) {
    ReaprInvariantProfile profile;
    profile.components = analyze_components(code, crossingless_components).total_components();
    profile.determinant_fingerprint =
        alexander_determinant_guard::alexander_determinant_fingerprint(code);
    if (include_alexander_roots) {
        profile.alexander_roots_evaluated = true;
        profile.alexander_roots_mod_11 = alexander_roots_mod_prime(code, 11, options);
        profile.alexander_roots_mod_19 = alexander_roots_mod_prime(code, 19, options);
        profile.alexander_roots_mod_31 = alexander_roots_mod_prime(code, 31, options);
    }
    return profile;
}

std::string profile_string(const ReaprInvariantProfile& profile) {
    std::ostringstream out;
    out << "components=" << profile.components
        << "; determinant="
        << alexander_determinant_guard::determinant_fingerprint_string(
               profile.determinant_fingerprint);
    if (profile.alexander_roots_evaluated) {
        out << "; alexander_roots_mod_11=" << int_vector_string(profile.alexander_roots_mod_11)
            << "; alexander_roots_mod_19=" << int_vector_string(profile.alexander_roots_mod_19)
            << "; alexander_roots_mod_31=" << int_vector_string(profile.alexander_roots_mod_31);
    } else {
        out << "; alexander_roots_mod_11=not_evaluated"
            << "; alexander_roots_mod_19=not_evaluated"
            << "; alexander_roots_mod_31=not_evaluated";
    }
    return out.str();
}

}  // namespace reapr_invariant_guard

PDCode torus_2_odd_pd_code(int crossings) {
    PDCode code;
    if (crossings <= 0 || crossings % 2 == 0) {
        return code;
    }
    code.reserve(static_cast<std::size_t>(crossings));
    code.push_back(Crossing{2 * crossings - 1, crossings, 0, crossings - 1});
    for (int i = 1; i < crossings; ++i) {
        if ((i % 2) != 0) {
            code.push_back(Crossing{
                crossings - i - 1,
                2 * crossings - i,
                crossings - i,
                2 * crossings - i - 1});
        } else {
            code.push_back(Crossing{
                2 * crossings - i - 1,
                crossings - i,
                2 * crossings - i,
                crossings - i - 1});
        }
    }
    return canonical_output_code(code);
}

std::uint64_t splitmix64_next(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

int deterministic_random_int(std::uint64_t& state, int low, int high) {
    if (high <= low) {
        return low;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(high - low + 1);
    return low + static_cast<int>(splitmix64_next(state) % span);
}

std::uint64_t reapr_seed_for_attempt(int attempt, int determinant, int crossings) {
    std::uint64_t state = 0xD6E8FEB86659FD93ULL;
    state ^= static_cast<std::uint64_t>(attempt + 1) * 0x9E3779B97F4A7C15ULL;
    state ^= static_cast<std::uint64_t>(std::max(0, determinant)) * 0xBF58476D1CE4E5B9ULL;
    state ^= static_cast<std::uint64_t>(std::max(0, crossings)) * 0x94D049BB133111EBULL;
    return splitmix64_next(state);
}

PDCode closed_braid_pd_code(int strands, const std::vector<int>& word) {
    if (strands < 2 || word.empty()) {
        return {};
    }

    std::vector<int> current(static_cast<std::size_t>(strands));
    std::iota(current.begin(), current.end(), 0);
    int next_label = strands;
    PDCode code;
    code.reserve(word.size());

    for (int generator : word) {
        const int position = std::abs(generator) - 1;
        if (position < 0 || position + 1 >= strands) {
            return {};
        }
        const int top_left = current[static_cast<std::size_t>(position)];
        const int top_right = current[static_cast<std::size_t>(position + 1)];
        const int bottom_left = next_label++;
        const int bottom_right = next_label++;
        if (generator > 0) {
            code.push_back(Crossing{top_right, bottom_right, bottom_left, top_left});
        } else {
            code.push_back(Crossing{top_right, top_left, bottom_left, bottom_right});
        }
        current[static_cast<std::size_t>(position)] = bottom_left;
        current[static_cast<std::size_t>(position + 1)] = bottom_right;
    }

    for (Crossing& crossing : code) {
        for (int& label : crossing) {
            if (0 <= label && label < strands) {
                label = current[static_cast<std::size_t>(label)];
            }
        }
    }

    try {
        return canonical_output_code(code);
    } catch (const std::exception&) {
        return {};
    }
}

std::vector<PDCode> reapr_candidate_codes_for_attempt(
    int determinant,
    int original_crossings,
    int attempt) {
    std::vector<PDCode> candidates;
    std::set<std::string> seen;
    const int maximum_candidate_crossings = std::max(0, original_crossings - 1);

    auto append_candidate = [&](const PDCode& candidate, bool allow_empty) {
        if (candidate.empty()) {
            if (allow_empty && seen.insert("PD[]").second) {
                candidates.push_back(candidate);
            }
            return;
        }
        if (static_cast<int>(candidate.size()) >= original_crossings) {
            return;
        }
        const std::string key = format_final_pd_code(candidate);
        if (seen.insert(key).second) {
            candidates.push_back(candidate);
        }
    };

    if (attempt == 0) {
        if (determinant == 1) {
            append_candidate(PDCode{}, true);
        } else if (determinant > 1 &&
                   (determinant % 2) != 0 &&
                   determinant < original_crossings) {
            append_candidate(torus_2_odd_pd_code(determinant), false);
        }
        return candidates;
    }

    if (maximum_candidate_crossings < 3) {
        return candidates;
    }

    std::uint64_t state = reapr_seed_for_attempt(attempt, determinant, original_crossings);

    if (determinant > 1 &&
        (determinant % 2) != 0 &&
        determinant <= maximum_candidate_crossings) {
        std::vector<int> mirror_word(static_cast<std::size_t>(determinant), -1);
        append_candidate(closed_braid_pd_code(2, mirror_word), false);
    }

    const int candidate_count = original_crossings > 160 ? 4 : 12;
    for (int candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
        const int strands = deterministic_random_int(state, 3, 7);
        int minimum_length = 3;
        if (determinant > 1 && determinant < maximum_candidate_crossings) {
            minimum_length = std::max(
                minimum_length,
                std::min(maximum_candidate_crossings, determinant - 4));
        }
        if (minimum_length > maximum_candidate_crossings) {
            minimum_length = maximum_candidate_crossings;
        }
        const int length = deterministic_random_int(state, minimum_length, maximum_candidate_crossings);
        std::vector<int> word;
        word.reserve(static_cast<std::size_t>(length));
        int previous = 0;
        for (int index = 0; index < length; ++index) {
            int generator = deterministic_random_int(state, 1, strands - 1);
            int sign = deterministic_random_int(state, 0, 99) < 40 ? -1 : 1;
            if (previous != 0 &&
                generator == std::abs(previous) &&
                sign == (previous > 0 ? -1 : 1)) {
                sign *= -1;
            }
            const int signed_generator = sign * generator;
            word.push_back(signed_generator);
            previous = signed_generator;
        }
        append_candidate(closed_braid_pd_code(strands, word), false);
    }

    return candidates;
}

struct ReaprOracleResult {
    bool accepted = false;
    bool rejected = false;
    int attempts = 0;
    PDCode code;
    std::size_t crossingless_components = 0;
    std::string status;
    std::string determinant_before;
    std::string determinant_after;
    std::string invariants_before;
    std::string invariants_after;
    std::vector<PDCode> matched_step_codes;
};

std::size_t crossingless_components_for_candidate(
    const PDCode& original,
    std::size_t original_crossingless,
    const PDCode& candidate) {
    const std::size_t total_components =
        analyze_components(original, original_crossingless).total_components();
    if (candidate.empty()) {
        return total_components;
    }
    const std::size_t candidate_crossing_components =
        analyze_components(candidate).components_with_crossings();
    return total_components > candidate_crossing_components
        ? total_components - candidate_crossing_components
        : 0;
}

ReaprOracleResult try_reapr_oracle(
    const PDCode& code,
    std::size_t crossingless_components,
    const SimplifierOptions& options) {
    ReaprOracleResult result;
    const auto before = alexander_determinant_guard::alexander_determinant_fingerprint(code);
    result.determinant_before = alexander_determinant_guard::determinant_fingerprint_string(before);
    result.determinant_after = result.determinant_before;

    const ComponentAnalysis components = analyze_components(code, crossingless_components);
    if (components.total_components() != 1) {
        result.status = "skipped_non_knot_input";
        return result;
    }
    if (code.empty()) {
        result.status = "skipped_empty_input";
        return result;
    }

    const int determinant = alexander_determinant_guard::single_small_determinant_value(before);
    if (determinant < 0) {
        result.status = "skipped_ambiguous_determinant_fingerprint";
        return result;
    }

    if (options.reapr_retry_max == 0) {
        result.status = "skipped_reapr_retry_max_zero";
        return result;
    }

    bool saw_candidate = false;
    bool saw_rejected_candidate = false;
    reapr_invariant_guard::ReaprInvariantProfile before_basic_profile;
    reapr_invariant_guard::ReaprInvariantProfile before_full_profile;
    bool have_before_basic_profile = false;
    bool have_before_full_profile = false;

    struct AcceptedCandidate {
        bool present = false;
        PDCode code;
        std::size_t crossingless_components = 0;
        std::size_t cleaned_crossings = 0;
        std::string status;
        std::string determinant_after;
        std::string invariants_after;
        std::string sort_key;
    };
    AcceptedCandidate best_candidate;

    auto store_candidate = [&](const PDCode& candidate,
                               std::size_t candidate_crossingless_components,
                               const PDCode& cleaned_code,
                               const std::string& status,
                               const std::string& determinant_after,
                               const std::string& invariants_after) {
        const std::string key = format_final_pd_code(cleaned_code);
        const bool better =
            !best_candidate.present ||
            cleaned_code.size() > best_candidate.cleaned_crossings ||
            (cleaned_code.size() == best_candidate.cleaned_crossings &&
             key < best_candidate.sort_key);
        if (!better) {
            return;
        }
        best_candidate.present = true;
        best_candidate.code = candidate;
        best_candidate.crossingless_components = candidate_crossingless_components;
        best_candidate.cleaned_crossings = cleaned_code.size();
        best_candidate.status = status;
        best_candidate.determinant_after = determinant_after;
        best_candidate.invariants_after = invariants_after;
        best_candidate.sort_key = key;
    };

    for (int attempt = 0; attempt < options.reapr_retry_max; ++attempt) {
        check_timeout(options);
        result.attempts = attempt + 1;
        const std::vector<PDCode> candidates =
            reapr_candidate_codes_for_attempt(
                determinant, static_cast<int>(code.size()), attempt);
        {
            std::ostringstream message;
            message << "reapr_attempt_start attempt=" << (attempt + 1)
                    << " retry_max=" << options.reapr_retry_max
                    << " candidates=" << candidates.size();
            emit_progress(options, message.str());
        }

        for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
            check_timeout(options);
            const PDCode& candidate = candidates[candidate_index];
            if (candidate.size() >= code.size()) {
                continue;
            }
            saw_candidate = true;

            const std::size_t candidate_crossingless_components =
                crossingless_components_for_candidate(code, crossingless_components, candidate);
            if (!have_before_basic_profile) {
                before_basic_profile = reapr_invariant_guard::profile_for_code(
                    code, crossingless_components, &options, false);
                have_before_basic_profile = true;
                result.determinant_before =
                    alexander_determinant_guard::determinant_fingerprint_string(
                        before_basic_profile.determinant_fingerprint);
                result.invariants_before =
                    reapr_invariant_guard::profile_string(before_basic_profile);
            }

            reapr_invariant_guard::ReaprInvariantProfile after_profile =
                reapr_invariant_guard::profile_for_code(
                    candidate, candidate_crossingless_components, &options, false);
            result.determinant_after =
                alexander_determinant_guard::determinant_fingerprint_string(
                    after_profile.determinant_fingerprint);
            result.invariants_after = reapr_invariant_guard::profile_string(after_profile);
            if (!reapr_invariant_guard::same_basic_profile(before_basic_profile, after_profile)) {
                saw_rejected_candidate = true;
                result.rejected = true;
                std::ostringstream message;
                message << "reapr_attempt_rejected attempt=" << (attempt + 1)
                        << " candidate=" << candidate_index
                        << " reason=basic_invariant_changed"
                        << " crossings=" << candidate.size();
                emit_progress(options, message.str());
                continue;
            }

            if (!have_before_full_profile) {
                before_full_profile = reapr_invariant_guard::profile_for_code(
                    code, crossingless_components, &options, true);
                have_before_full_profile = true;
                result.invariants_before =
                    reapr_invariant_guard::profile_string(before_full_profile);
            }
            after_profile = reapr_invariant_guard::profile_for_code(
                candidate, candidate_crossingless_components, &options, true);
            result.invariants_after = reapr_invariant_guard::profile_string(after_profile);
            if (!reapr_invariant_guard::same_profile(before_full_profile, after_profile)) {
                saw_rejected_candidate = true;
                result.rejected = true;
                std::ostringstream message;
                message << "reapr_attempt_rejected attempt=" << (attempt + 1)
                        << " candidate=" << candidate_index
                        << " reason=full_invariant_changed"
                        << " crossings=" << candidate.size();
                emit_progress(options, message.str());
                continue;
            }

            check_timeout(options);
            const PDSimplificationResult cleaned_candidate =
                simplify_pd_code(candidate, candidate_crossingless_components);
            check_timeout(options);
            const PDCode cleaned_code = canonical_output_code(cleaned_candidate.code);

            const std::string accepted_status = candidate.empty()
                ? "accepted_empty_projection"
                : (attempt == 0 ? "accepted_projection_template"
                                : "accepted_retry_projection");
            store_candidate(
                candidate,
                candidate_crossingless_components,
                cleaned_code,
                accepted_status,
                result.determinant_after,
                result.invariants_after);
            result.matched_step_codes.push_back(canonical_output_code(candidate));
            {
                std::ostringstream message;
                message << "reapr_attempt_matched attempt=" << (attempt + 1)
                        << " candidate=" << candidate_index
                        << " crossings=" << candidate.size()
                        << " cleaned_crossings=" << cleaned_code.size();
                emit_progress(options, message.str());
            }
            if (cleaned_code.size() + 1 == code.size()) {
                result.accepted = true;
                result.status = best_candidate.status;
                result.code = best_candidate.code;
                result.crossingless_components = best_candidate.crossingless_components;
                result.determinant_after = best_candidate.determinant_after;
                result.invariants_after = best_candidate.invariants_after;
                return result;
            }
        }
    }

    if (best_candidate.present) {
        result.accepted = true;
        result.status = best_candidate.status;
        result.code = best_candidate.code;
        result.crossingless_components = best_candidate.crossingless_components;
        result.determinant_after = best_candidate.determinant_after;
        result.invariants_after = best_candidate.invariants_after;
    } else if (saw_rejected_candidate) {
        result.status = "rejected_invariant_changed";
    } else if (saw_candidate) {
        result.status = "rejected_no_matching_profile";
    } else {
        result.status = "skipped_no_smaller_projection_template";
    }
    return result;
}

}  // namespace

inline PDCode parse_pd_code(const std::string& text) {
    std::vector<int> numbers;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '-' || std::isdigit(static_cast<unsigned char>(text[i]))) {
            const std::size_t start = i;
            if (text[i] == '-') {
                ++i;
                if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
                    throw std::invalid_argument("A minus sign must be followed by digits");
                }
            }
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            const std::string token = text.substr(start, i - start);
            numbers.push_back(std::stoi(token));
        } else {
            ++i;
        }
    }

    if (numbers.empty()) {
        return {};
    }
    if (numbers.size() % 4 != 0) {
        throw std::invalid_argument("The input must contain a multiple of four integers");
    }

    PDCode code;
    code.reserve(numbers.size() / 4);
    for (std::size_t i = 0; i < numbers.size(); i += 4) {
        code.push_back(Crossing{numbers[i], numbers[i + 1], numbers[i + 2], numbers[i + 3]});
    }
    return code;
}

inline std::string format_pd_code(const PDCode& code) {
    std::ostringstream out;
    out << "PD[";
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "X[" << code[i][0] << ',' << code[i][1] << ','
            << code[i][2] << ',' << code[i][3] << ']';
    }
    out << ']';
    return out.str();
}

inline std::string format_final_pd_code(const PDCode& code) {
    if (code.empty()) {
        return format_pd_code(code);
    }

    const Diagram diagram(code);
    PDCode oriented(code.size());
    std::set<int> labels;
    std::map<int, int> next_label;

    for (int crossing = 0; crossing < static_cast<int>(code.size()); ++crossing) {
        for (int strand = 0; strand < 4; ++strand) {
            const int label = diagram.label_at(crossing, strand);
            oriented[crossing][strand] = label;
            labels.insert(label);
        }

        if (!diagram.crossings[crossing].directions[0][2]) {
            throw std::invalid_argument("Could not orient final PD crossing from an under-incoming strand");
        }
        for (int tail = 0; tail < 4; ++tail) {
            for (int head = 0; head < 4; ++head) {
                if (!diagram.crossings[crossing].directions[tail][head]) {
                    continue;
                }
                const int in_label = diagram.label_at(crossing, tail);
                const int out_label = diagram.label_at(crossing, head);
                const auto inserted = next_label.insert(std::make_pair(in_label, out_label));
                if (!inserted.second && inserted.first->second != out_label) {
                    throw std::invalid_argument("Final PD component orientation is inconsistent");
                }
            }
        }
    }

    std::map<int, int> relabel;
    int next_output_label = 1;
    for (int start : labels) {
        if (relabel.count(start) != 0) {
            continue;
        }

        int current = start;
        while (relabel.count(current) == 0) {
            relabel[current] = next_output_label++;
            const auto next = next_label.find(current);
            if (next == next_label.end()) {
                throw std::invalid_argument("Final PD component orientation is incomplete");
            }
            current = next->second;
        }
        if (current != start) {
            throw std::invalid_argument("Final PD component orientation reached another component");
        }
    }

    for (Crossing& crossing : oriented) {
        for (int& label : crossing) {
            label = relabel.at(label);
        }
    }
    std::sort(oriented.begin(), oriented.end());
    return format_pd_code(oriented);
}

inline std::string format_endpoint(const Endpoint& endpoint) {
    std::ostringstream out;
    out << '(' << endpoint.crossing << ", " << endpoint.strand << ')';
    return out.str();
}

inline std::string format_direction(Direction direction) {
    return direction == Direction::Left ? "left" : "right";
}

inline ComponentAnalysis analyze_components(
    const PDCode& code,
    std::size_t known_crossingless_components) {
    ComponentAnalysis analysis;
    analysis.crossingless_components = known_crossingless_components;
    if (code.empty()) {
        return analysis;
    }

    const std::string cache_key =
        std::to_string(known_crossingless_components) + "|" + raw_code_cache_key(code);
    auto& cache = shared_pure_function_cache<ComponentAnalysis>();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto cached = cache.values.find(cache_key);
        if (cached != cache.values.end()) {
            return cached->second;
        }
    }

    Diagram diagram(code);
    analysis.components = component_summaries(diagram);
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        trim_pure_function_cache(cache.values);
        cache.values.emplace(cache_key, analysis);
    }
    return analysis;
}

inline ComponentAnalysis analyze_components_after_removing_crossings(
    const PDCode& code,
    const std::vector<int>& removed_crossings,
    std::size_t known_crossingless_components) {
    const std::set<int> removed = normalized_removed_crossings(code, removed_crossings);
    ComponentAnalysis original = analyze_components(code, known_crossingless_components);
    ComponentAnalysis reduced;
    reduced.crossingless_components = original.crossingless_components;

    for (const LinkComponentSummary& component : original.components) {
        LinkComponentSummary remaining_component;
        for (int crossing : component.crossing_indices) {
            if (removed.count(crossing) == 0) {
                remaining_component.crossing_indices.push_back(crossing);
            }
        }

        if (remaining_component.crossing_indices.empty()) {
            ++reduced.crossingless_components;
        } else {
            reduced.components.push_back(std::move(remaining_component));
        }
    }

    return reduced;
}

inline std::size_t count_crossingless_components_after_removing_crossings(
    const PDCode& code,
    const std::vector<int>& removed_crossings,
    std::size_t known_crossingless_components) {
    return analyze_components_after_removing_crossings(
        code, removed_crossings, known_crossingless_components).crossingless_components;
}

inline bool apply_reverse_type_i(PDCode& code, std::mt19937& rng) {
    if (code.empty()) {
        return false;
    }

    const LabelMap labels = build_label_map(code);
    std::uniform_int_distribution<int> endpoint_distribution(
        0, static_cast<int>(code.size() * 4 - 1));
    const Endpoint first = endpoint_from_key(endpoint_distribution(rng));
    const Endpoint second = mate_endpoint(code, labels, first);

    const int first_label = max_label(code) + 1;
    const int second_label = first_label + 1;
    const int loop_label = first_label + 2;

    std::uniform_int_distribution<int> hand_distribution(0, 1);
    code[first.crossing][first.strand] = first_label;
    code[second.crossing][second.strand] = second_label;
    if (hand_distribution(rng) == 0) {
        code.push_back(Crossing{first_label, loop_label, loop_label, second_label});
    } else {
        code.push_back(Crossing{first_label, second_label, loop_label, loop_label});
    }
    return true;
}

inline bool apply_reverse_type_ii(PDCode& code, std::mt19937& rng) {
    if (code.empty()) {
        return false;
    }

    const LabelMap labels = build_label_map(code);
    const std::vector<std::vector<int>> faces = raw_faces_from_pd_code(code);
    std::vector<int> eligible_faces;
    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
        if (faces[i].size() > 1) {
            eligible_faces.push_back(i);
        }
    }
    if (eligible_faces.empty()) {
        return false;
    }

    std::uniform_int_distribution<int> face_distribution(0, static_cast<int>(eligible_faces.size() - 1));
    for (int attempt = 0; attempt < 50; ++attempt) {
        const std::vector<int>& face = faces[eligible_faces[face_distribution(rng)]];
        std::uniform_int_distribution<int> corner_distribution(0, static_cast<int>(face.size() - 1));
        const int first_corner = corner_distribution(rng);
        int second_corner = corner_distribution(rng);
        if (first_corner == second_corner) {
            second_corner = (second_corner + 1) % static_cast<int>(face.size());
        }

        const Endpoint c = endpoint_from_key(face[first_corner]);
        const Endpoint d = endpoint_from_key(face[second_corner]);
        const Endpoint c_opposite = mate_endpoint(code, labels, c);
        const Endpoint d_opposite = mate_endpoint(code, labels, d);

        const std::set<int> touched{
            endpoint_key(c),
            endpoint_key(c_opposite),
            endpoint_key(d),
            endpoint_key(d_opposite)};
        if (touched.size() != 4) {
            continue;
        }

        const int base = max_label(code) + 1;
        const int d_opposite_label = base;
        const int c_label = base + 1;
        const int shared_first = base + 2;
        const int shared_second = base + 3;
        const int c_opposite_label = base + 4;
        const int d_label = base + 5;

        code[d_opposite.crossing][d_opposite.strand] = d_opposite_label;
        code[c.crossing][c.strand] = c_label;
        code[c_opposite.crossing][c_opposite.strand] = c_opposite_label;
        code[d.crossing][d.strand] = d_label;

        code.push_back(Crossing{d_opposite_label, c_label, shared_first, shared_second});
        code.push_back(Crossing{shared_first, c_opposite_label, d_label, shared_second});
        return true;
    }

    return false;
}

inline RandomInflationResult randomly_increase_crossings(
    const PDCode& code,
    const RandomInflationOptions& options) {
    if (options.moves < 0) {
        throw std::invalid_argument("Random inflation move count cannot be negative");
    }
    if (options.type_ii_percentage < 0 || options.type_ii_percentage > 100) {
        throw std::invalid_argument("Type-II percentage must be between 0 and 100");
    }

    RandomInflationResult result;
    result.code = code;
    result.seed = options.seed;
    std::mt19937 rng(options.seed);
    std::uniform_int_distribution<int> percent_distribution(0, 99);

    for (int move = 0; move < options.moves; ++move) {
        const bool prefer_type_ii = percent_distribution(rng) < options.type_ii_percentage;
        if (prefer_type_ii && apply_reverse_type_ii(result.code, rng)) {
            ++result.type_ii_moves;
        } else if (apply_reverse_type_i(result.code, rng)) {
            ++result.type_i_moves;
        } else if (apply_reverse_type_ii(result.code, rng)) {
            ++result.type_ii_moves;
        } else {
            throw std::runtime_error("Could not apply any random crossing-increasing move");
        }
    }

    return result;
}

inline PDSimplificationResult simplify_pd_code_checked(
    const PDCode& code,
    std::size_t known_crossingless_components,
    const SimplifierOptions* options,
    bool allow_reidemeister_ii = true,
    bool canonicalize_input = true) {
    if (options != nullptr) {
        check_timeout(*options);
    }
    PDSimplificationResult result;
    result.code = canonicalize_input ? canonical_output_code(code) : code;
    result.crossingless_components = known_crossingless_components;

    while (true) {
        if (options != nullptr) {
            check_timeout(*options);
        }
        const int r1_before = result.reidemeister_i_moves;
        result.code = erase_r1_moves(
            result.code,
            result.crossingless_components,
            result.reidemeister_i_moves,
            canonicalize_input);
        if (result.reidemeister_i_moves != r1_before) {
            continue;
        }

        if (allow_reidemeister_ii) {
            if (options != nullptr) {
                check_timeout(*options);
            }
            const ReidemeisterIIMove r2_move = find_reidemeister_ii_move(result.code);
            if (r2_move.found) {
                result.code = erase_one_reidemeister_ii_move(
                    result.code,
                    r2_move,
                    result.crossingless_components,
                    result.reidemeister_ii_moves);
                continue;
            }
        }

        if (options != nullptr) {
            check_timeout(*options);
        }
        const int crossing_index = find_nugatory_crossing(result.code);
        if (crossing_index < 0) {
            break;
        }
        result.code = erase_one_nugatory_crossing(
            result.code,
            crossing_index,
            result.crossingless_components,
            result.nugatory_crossing_moves,
            canonicalize_input);
    }

    return result;
}

inline PDSimplificationResult simplify_pd_code(
    const PDCode& code,
    std::size_t known_crossingless_components) {
    return simplify_pd_code_checked(code, known_crossingless_components, nullptr);
}

struct ReidemeisterIIIFailoverResult {
    bool found = false;
    PDCode code;
    std::size_t crossingless_components = 0;
    int depth = 0;
    int visited_states = 0;
    int reidemeister_i_moves = 0;
    int reidemeister_ii_moves = 0;
    int reidemeister_iii_moves = 0;
    int nugatory_crossing_moves = 0;
};

inline ReidemeisterIIIFailoverResult find_reidemeister_iii_failover(
    const PDCode& code,
    std::size_t crossingless_components,
    const SimplifierOptions& options,
    int max_depth = kR3FailoverMaxDepth,
    int max_states = kR3FailoverMaxStates) {
    check_timeout(options);
    ReidemeisterIIIFailoverResult result;
    result.code = code;
    result.crossingless_components = crossingless_components;
    const std::size_t target_crossings = code.size();
    if (target_crossings < 3) {
        return result;
    }

    struct State {
        PDCode code;
        std::size_t crossingless_components = 0;
        int depth = 0;
    };

    std::deque<State> queue;
    queue.push_back(State{code, crossingless_components, 0});
    std::set<std::string> seen;
    seen.insert(format_final_pd_code(code));

    while (!queue.empty() && static_cast<int>(seen.size()) <= max_states) {
        check_timeout(options);
        const State state = queue.front();
        queue.pop_front();
        ++result.visited_states;
        if (state.depth >= max_depth) {
            continue;
        }

        const std::vector<ReidemeisterIIIMove> moves =
            possible_reidemeister_iii_moves(state.code);
        for (const ReidemeisterIIIMove& move : moves) {
            check_timeout(options);
            const PDCode moved = apply_reidemeister_iii_move(state.code, move);
            PDSimplificationResult simplified =
                simplify_pd_code_checked(moved, state.crossingless_components, &options);
            if (simplified.code.size() < target_crossings) {
                result.found = true;
                result.code = canonical_output_code(simplified.code);
                result.crossingless_components = simplified.crossingless_components;
                result.depth = state.depth + 1;
                result.reidemeister_i_moves = simplified.reidemeister_i_moves;
                result.reidemeister_ii_moves = simplified.reidemeister_ii_moves;
                result.reidemeister_iii_moves = state.depth + 1;
                result.nugatory_crossing_moves = simplified.nugatory_crossing_moves;
                return result;
            }
            if (simplified.code.size() != target_crossings) {
                continue;
            }

            const std::string key = format_final_pd_code(simplified.code);
            if (seen.insert(key).second) {
                queue.push_back(State{
                    canonical_output_code(simplified.code),
                    simplified.crossingless_components,
                    state.depth + 1});
                if (static_cast<int>(seen.size()) >= max_states) {
                    break;
                }
            }
        }
    }

    return result;
}

struct NonMonotoneStep {
    PDCode code;
    std::size_t crossingless_components = 0;
    std::string kind;
    int red_length = 0;
    int green_length = 0;
    int reidemeister_i_moves = 0;
    int reidemeister_ii_moves = 0;
    int reidemeister_iii_moves = 0;
    int nugatory_crossing_moves = 0;
};

struct NonMonotoneNode {
    PDCode code;
    std::size_t crossingless_components = 0;
    std::vector<NonMonotoneStep> steps;
    int depth = 0;
    int r3_potential = 0;
    int serial = 0;
};

struct NonMonotoneSearchResult {
    bool found = false;
    PDCode code;
    std::size_t crossingless_components = 0;
    std::vector<NonMonotoneStep> steps;
    std::size_t tested_red_paths = 0;
    std::size_t tested_green_paths = 0;
    std::size_t applied_candidates = 0;
    std::size_t generated_states = 0;
    int depth = 0;
    int reidemeister_i_moves = 0;
    int reidemeister_ii_moves = 0;
    int reidemeister_iii_moves = 0;
    int nugatory_crossing_moves = 0;
};

inline int safe_r3_potential(const PDCode& code, const SimplifierOptions& options) {
    check_timeout(options);
    if (code.size() < 3) {
        return 0;
    }
    return static_cast<int>(possible_reidemeister_iii_moves(code).size());
}

inline bool non_monotone_node_less(
    const NonMonotoneNode& lhs,
    const NonMonotoneNode& rhs,
    std::size_t target_crossings) {
    const int lhs_delta = static_cast<int>(lhs.code.size()) - static_cast<int>(target_crossings);
    const int rhs_delta = static_cast<int>(rhs.code.size()) - static_cast<int>(target_crossings);
    if (lhs_delta != rhs_delta) {
        return lhs_delta < rhs_delta;
    }
    if (lhs.r3_potential != rhs.r3_potential) {
        return lhs.r3_potential > rhs.r3_potential;
    }
    if (lhs.depth != rhs.depth) {
        return lhs.depth < rhs.depth;
    }
    if (lhs.steps.size() != rhs.steps.size()) {
        return lhs.steps.size() < rhs.steps.size();
    }
    return lhs.serial < rhs.serial;
}

inline void accumulate_non_monotone_counts(
    NonMonotoneSearchResult& result,
    const NonMonotoneStep& step) {
    result.reidemeister_i_moves += step.reidemeister_i_moves;
    result.reidemeister_ii_moves += step.reidemeister_ii_moves;
    result.reidemeister_iii_moves += step.reidemeister_iii_moves;
    result.nugatory_crossing_moves += step.nugatory_crossing_moves;
}

inline bool add_non_monotone_candidate(
    const NonMonotoneNode& parent,
    const PDCode& raw_code,
    std::size_t raw_crossingless_components,
    const NonMonotoneStep& raw_step,
    const std::set<std::string>& accepted_states,
    std::set<std::string>& candidate_states,
    std::size_t max_allowed_crossings,
    std::size_t target_crossings,
    int& serial,
    const SimplifierOptions& options,
    std::vector<NonMonotoneNode>& candidates,
    NonMonotoneSearchResult& result) {
    check_timeout(options);
    const std::string key = format_final_pd_code(raw_code);
    if (accepted_states.count(key) != 0 || !candidate_states.insert(key).second) {
        return false;
    }
    PDCode code = parse_pd_code(key);
    if (code.size() > max_allowed_crossings) {
        return false;
    }

    NonMonotoneStep step = raw_step;
    step.code = code;
    step.crossingless_components = raw_crossingless_components;

    NonMonotoneNode node;
    node.code = std::move(code);
    node.crossingless_components = raw_crossingless_components;
    node.steps = parent.steps;
    node.steps.push_back(std::move(step));
    node.depth = parent.depth + 1;
    node.r3_potential = safe_r3_potential(node.code, options);
    node.serial = serial++;

    ++result.generated_states;
    if (node.code.size() < target_crossings) {
        result.found = true;
        result.code = node.code;
        result.crossingless_components = node.crossingless_components;
        result.steps = node.steps;
        result.depth = node.depth;
        for (const NonMonotoneStep& stored_step : result.steps) {
            accumulate_non_monotone_counts(result, stored_step);
        }
        return true;
    }

    candidates.push_back(std::move(node));
    return true;
}

inline void generate_non_monotone_r3_candidates(
    const NonMonotoneNode& node,
    const std::set<std::string>& accepted_states,
    std::set<std::string>& candidate_states,
    std::size_t max_allowed_crossings,
    std::size_t target_crossings,
    int& serial,
    const SimplifierOptions& options,
    std::vector<NonMonotoneNode>& candidates,
    NonMonotoneSearchResult& result) {
    check_timeout(options);
    if (static_cast<int>(candidates.size()) >= kNonMonotoneMaxCandidatesPerState) {
        return;
    }
    if (options.verbose) {
        std::ostringstream message;
        message << "non_monotone_r3_start node_depth=" << node.depth
                << " crossings=" << node.code.size()
                << " candidates=" << candidates.size();
        emit_progress(options, message.str());
    }
    std::vector<ReidemeisterIIIMove> moves = possible_reidemeister_iii_moves(node.code);
    int tried = 0;
    for (const ReidemeisterIIIMove& move : moves) {
        check_timeout(options);
        if (tried >= kNonMonotoneR3MovesPerState ||
            static_cast<int>(candidates.size()) >= kNonMonotoneMaxCandidatesPerState) {
            break;
        }
        ++tried;
        const PDCode moved = apply_reidemeister_iii_move(node.code, move);
        const PDSimplificationResult simplified =
            simplify_pd_code_checked(moved, node.crossingless_components, &options);

        NonMonotoneStep step;
        step.kind = "r3";
        step.reidemeister_i_moves = simplified.reidemeister_i_moves;
        step.reidemeister_ii_moves = simplified.reidemeister_ii_moves;
        step.reidemeister_iii_moves = 1;
        step.nugatory_crossing_moves = simplified.nugatory_crossing_moves;

        (void)add_non_monotone_candidate(
                node,
                simplified.code,
                simplified.crossingless_components,
                step,
                accepted_states,
                candidate_states,
                max_allowed_crossings,
                target_crossings,
                serial,
                options,
                candidates,
                result);
        if (result.found) {
            return;
        }
    }
    if (options.verbose) {
        std::ostringstream message;
        message << "non_monotone_r3_done node_depth=" << node.depth
                << " tried=" << tried
                << " candidates=" << candidates.size()
                << " generated_states=" << result.generated_states;
        emit_progress(options, message.str());
    }
}

inline void generate_non_monotone_surgery_candidates(
    const NonMonotoneNode& node,
    const std::set<std::string>& accepted_states,
    std::set<std::string>& candidate_states,
    std::size_t max_allowed_crossings,
    std::size_t target_crossings,
    int& serial,
    std::size_t& total_green_tests,
    const SimplifierOptions& options,
    std::vector<NonMonotoneNode>& candidates,
    NonMonotoneSearchResult& result) {
    check_timeout(options);
    const std::string state_key = format_final_pd_code(node.code);
    const std::uint64_t state_hash = stable_hash_text(state_key);
    const Diagram diagram(node.code);
    const DualGraph base_graph(diagram);
    const std::vector<std::vector<Endpoint>> red_lines = possible_red_lines(diagram);

    std::map<int, std::vector<std::size_t>> red_by_length;
    for (std::size_t i = 0; i < red_lines.size(); ++i) {
        const int length = static_cast<int>(red_lines[i].size());
        if (length > kNonMonotoneMaxRedLength) {
            break;
        }
        red_by_length[length].push_back(i);
    }

    std::size_t state_green_tests = 0;
    int state_red_tests = 0;
    std::vector<int> length_order;
    length_order.reserve(red_by_length.size());
    for (const auto& item : red_by_length) {
        length_order.push_back(item.first);
    }
    const std::size_t length_start = 0;

    for (std::size_t length_offset = 0; length_offset < length_order.size(); ++length_offset) {
        const int red_length = length_order[(length_start + length_offset) % length_order.size()];
        const std::vector<std::size_t>& indices = red_by_length.at(red_length);
        if (indices.empty()) {
            continue;
        }
        int accepted_for_length = 0;
        bool length_done = false;
        const std::size_t start_slot =
            static_cast<std::size_t>(
                (state_hash + static_cast<std::uint64_t>(red_length) * 11400714819323198485ULL) %
                static_cast<std::uint64_t>(indices.size()));
        const std::size_t scan_limit =
            std::min<std::size_t>(indices.size(), kNonMonotoneMaxRedScansPerLength);

        for (std::size_t slot_offset = 0; slot_offset < scan_limit; ++slot_offset) {
            check_timeout(options);
            if (result.found ||
                static_cast<int>(candidates.size()) >= kNonMonotoneMaxCandidatesPerState ||
                state_red_tests >= kNonMonotoneMaxRedTestsPerNode ||
                state_green_tests >= kNonMonotoneMaxGreenTestsPerState ||
                total_green_tests >= kNonMonotoneMaxTotalGreenTests) {
                return;
            }
            if (accepted_for_length >= kNonMonotoneMaxCandidatesPerLength) {
                break;
            }
            const std::size_t red_index =
                indices[(start_slot + slot_offset) % indices.size()];
            const auto& red_path = red_lines[red_index];
            ++state_red_tests;
            ++result.tested_red_paths;
            if (options.verbose && (result.tested_red_paths <= 8 || result.tested_red_paths % 64 == 0)) {
                std::ostringstream message;
                message << "non_monotone_progress node_depth=" << node.depth
                        << " red_length=" << red_length
                        << " tested_red=" << result.tested_red_paths
                        << " tested_green=" << result.tested_green_paths
                        << " applied_candidates=" << result.applied_candidates
                        << " candidates=" << candidates.size()
                        << " state_green_tests=" << state_green_tests
                        << " total_green_tests=" << total_green_tests;
                emit_progress(options, message.str());
            }

            DualGraph graph = base_graph;
            const Endpoint start = red_path.front();
            const Endpoint end = red_path.back();
            const std::array<int, 2> sources{
                graph.edge_to_face[endpoint_key(start)],
                graph.edge_to_face[endpoint_key(diagram.opposite(start))]};
            const std::array<int, 2> destinations{
                graph.edge_to_face[endpoint_key(end)],
                graph.edge_to_face[endpoint_key(diagram.opposite(end))]};

            for (std::size_t i = 1; i + 1 < red_path.size(); ++i) {
                const Endpoint endpoint = red_path[i];
                const int right_region = graph.edge_to_face[endpoint_key(endpoint)];
                const int left_region = graph.edge_to_face[endpoint_key(diagram.opposite(endpoint))];
                if (GraphEdge* edge = graph.mutable_edge(right_region, left_region)) {
                    edge->weight = kBlockedWeight;
                }
            }

            const int cutoff = red_length + kNonMonotoneExtraCrossings;
            for (int source : sources) {
                for (int destination : destinations) {
                    check_timeout(options);
                    std::vector<std::vector<int>> green_paths =
                        collect_limited_heuristic_paths(
                            graph,
                            source,
                            destination,
                            cutoff,
                            kNonMonotoneHeuristicStateBudget,
                            kNonMonotoneHeuristicPathBudget,
                            options);
                    for (const std::vector<int>& green_path : green_paths) {
                        check_timeout(options);
                        if (result.found ||
                            static_cast<int>(candidates.size()) >= kNonMonotoneMaxCandidatesPerState ||
                            state_green_tests >= kNonMonotoneMaxGreenTestsPerState ||
                            total_green_tests >= kNonMonotoneMaxTotalGreenTests) {
                            return;
                        }
                        if (accepted_for_length >= kNonMonotoneMaxCandidatesPerLength) {
                            length_done = true;
                            break;
                        }
                        if (static_cast<int>(green_path.size()) > red_length + kNonMonotoneExtraCrossings) {
                            continue;
                        }
                        ++state_green_tests;
                        ++total_green_tests;
                        ++result.tested_green_paths;

                        for (Direction direction : {Direction::Left, Direction::Right}) {
                            SimplificationResult witness;
                            witness.path_search_mode = "non_monotone";
                            if (!do_check(diagram, graph, red_path, green_path, direction, witness, options)) {
                                continue;
                            }

                            try {
                                const MidSimplificationApplyResult applied =
                                    apply_simplification_witness(
                                        node.code,
                                        witness,
                                        node.crossingless_components);
                                const PDSimplificationResult simplified =
                                    simplify_pd_code_checked(
                                        applied.code,
                                        applied.crossingless_components,
                                        &options);
                                ++result.applied_candidates;

                                NonMonotoneStep step;
                                step.kind = "surgery";
                                step.red_length = red_length;
                                step.green_length = static_cast<int>(green_path.size());
                                step.reidemeister_i_moves = simplified.reidemeister_i_moves;
                                step.reidemeister_ii_moves = simplified.reidemeister_ii_moves;
                                step.nugatory_crossing_moves = simplified.nugatory_crossing_moves;

                                const bool accepted = add_non_monotone_candidate(
                                    node,
                                    simplified.code,
                                    simplified.crossingless_components,
                                    step,
                                    accepted_states,
                                    candidate_states,
                                    max_allowed_crossings,
                                    target_crossings,
                                    serial,
                                    options,
                                    candidates,
                                    result);
                                if (result.found) {
                                    return;
                                }
                                if (accepted) {
                                    ++accepted_for_length;
                                    if (accepted_for_length >= kNonMonotoneMaxCandidatesPerLength) {
                                        length_done = true;
                                        break;
                                    }
                                }
                            } catch (const std::exception&) {
                                continue;
                            }
                        }
                        if (length_done) {
                            break;
                        }
                    }
                    if (length_done) {
                        break;
                    }
                }
                if (length_done) {
                    break;
                }
            }
            if (length_done) {
                break;
            }
        }
    }
}

inline std::vector<NonMonotoneNode> select_non_monotone_beam(
    std::vector<NonMonotoneNode> candidates,
    std::size_t target_crossings) {
    std::vector<NonMonotoneNode> selected;
    selected.reserve(kNonMonotoneBeamWidth);
    std::set<int> selected_serials;

    auto take_candidate = [&](NonMonotoneNode& candidate, std::map<int, int>& selected_by_delta) {
        const int delta =
            static_cast<int>(candidate.code.size()) - static_cast<int>(target_crossings);
        if (selected_serials.count(candidate.serial) != 0) {
            return;
        }
        const int same_crossing_cap = std::max(3, kNonMonotoneBeamWidth / 3);
        const int other_crossing_cap = std::max(2, kNonMonotoneBeamWidth / 5);
        const int cap = delta == 0 ? same_crossing_cap : other_crossing_cap;
        if (selected_by_delta[delta] >= cap && selected.size() + 1 < kNonMonotoneBeamWidth) {
            return;
        }
        ++selected_by_delta[delta];
        selected_serials.insert(candidate.serial);
        selected.push_back(candidate);
    };

    std::sort(
        candidates.begin(),
        candidates.end(),
        [&](const NonMonotoneNode& lhs, const NonMonotoneNode& rhs) {
            return non_monotone_node_less(lhs, rhs, target_crossings);
        });

    std::map<int, int> crossing_first_by_delta;
    for (NonMonotoneNode& candidate : candidates) {
        take_candidate(candidate, crossing_first_by_delta);
        if (static_cast<int>(selected.size()) >= kNonMonotoneBeamWidth / 2) {
            break;
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [&](const NonMonotoneNode& lhs, const NonMonotoneNode& rhs) {
            if (lhs.r3_potential != rhs.r3_potential) {
                return lhs.r3_potential > rhs.r3_potential;
            }
            return non_monotone_node_less(lhs, rhs, target_crossings);
        });

    std::map<int, int> r3_first_by_delta;
    for (NonMonotoneNode& candidate : candidates) {
        take_candidate(candidate, r3_first_by_delta);
        if (static_cast<int>(selected.size()) >= kNonMonotoneBeamWidth) {
            break;
        }
    }
    return selected;
}

inline NonMonotoneSearchResult find_non_monotone_reduction(
    const PDCode& code,
    std::size_t crossingless_components,
    const SimplifierOptions& options) {
    check_timeout(options);
    NonMonotoneSearchResult result;
    result.code = code;
    result.crossingless_components = crossingless_components;
    const std::size_t target_crossings = code.size();
    const std::size_t max_allowed_crossings =
        target_crossings + static_cast<std::size_t>(kNonMonotoneMaxTotalIncrease);

    std::set<std::string> accepted_states;
    accepted_states.insert(format_final_pd_code(code));

    int serial = 0;
    NonMonotoneNode initial;
    initial.code = code;
    initial.crossingless_components = crossingless_components;
    initial.depth = 0;
    initial.r3_potential = safe_r3_potential(code, options);
    initial.serial = serial++;

    std::vector<NonMonotoneNode> beam;
    beam.push_back(std::move(initial));
    std::size_t total_green_tests = 0;

    for (int depth = 0; depth < kNonMonotoneMaxDepth && !beam.empty(); ++depth) {
        check_timeout(options);
        std::vector<NonMonotoneNode> candidates;
        std::set<std::string> candidate_states;
        for (const NonMonotoneNode& node : beam) {
            check_timeout(options);
            generate_non_monotone_r3_candidates(
                node,
                accepted_states,
                candidate_states,
                max_allowed_crossings,
                target_crossings,
                serial,
                options,
                candidates,
                result);
            if (result.found) {
                return result;
            }
            if (static_cast<int>(candidates.size()) >= kNonMonotoneMaxCandidatesPerState) {
                break;
            }
            generate_non_monotone_surgery_candidates(
                node,
                accepted_states,
                candidate_states,
                max_allowed_crossings,
                target_crossings,
                serial,
                total_green_tests,
                options,
                candidates,
                result);
            if (result.found) {
                return result;
            }
            if (static_cast<int>(candidates.size()) >= kNonMonotoneMaxCandidatesPerState) {
                break;
            }
            if (total_green_tests >= kNonMonotoneMaxTotalGreenTests) {
                break;
            }
        }

        beam = select_non_monotone_beam(std::move(candidates), target_crossings);
        for (const NonMonotoneNode& node : beam) {
            accepted_states.insert(format_final_pd_code(node.code));
        }
        if (options.verbose) {
            std::ostringstream message;
            message << "non_monotone_depth depth=" << (depth + 1)
                    << " beam=" << beam.size()
                    << " generated_states=" << result.generated_states
                    << " tested_red=" << result.tested_red_paths
                    << " tested_green=" << result.tested_green_paths
                    << " applied_candidates=" << result.applied_candidates
                    << " total_green_budget=" << total_green_tests;
            if (!beam.empty()) {
                message << " best_crossings=" << beam.front().code.size()
                        << " best_r3_potential=" << beam.front().r3_potential;
            }
            emit_progress(options, message.str());
        }
        if (total_green_tests >= kNonMonotoneMaxTotalGreenTests) {
            break;
        }
    }

    return result;
}

namespace {

int detected_worker_count() {
    const unsigned int reported = std::thread::hardware_concurrency();
    if (reported == 0) {
        return 1;
    }
    if (reported <= 2) {
        return static_cast<int>(reported);
    }
    return static_cast<int>(reported - 1);
}

int selected_bruteforce_worker_count(int max_threads, int task_count) {
    if (task_count <= 1) {
        return 1;
    }
    int requested = max_threads == -1 ? detected_worker_count() : max_threads;
    if (requested < 1) {
        requested = 1;
    }
    if (max_threads == -1 && task_count < 32) {
        return 1;
    }
    return std::max(1, std::min(requested, task_count));
}

bool should_skip_parallel_red_path(
    int red_index,
    const std::atomic<int>* best_found_index) {
    return best_found_index != nullptr &&
           red_index > best_found_index->load(std::memory_order_relaxed);
}

void record_parallel_found_index(
    int red_index,
    std::atomic<int>* best_found_index) {
    if (best_found_index == nullptr) {
        return;
    }
    int observed = best_found_index->load(std::memory_order_relaxed);
    while (red_index < observed &&
           !best_found_index->compare_exchange_weak(
               observed,
               red_index,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

struct RedPathSearchOutcome {
    bool completed = false;
    bool skipped = false;
    bool found = false;
    bool resource_limited = false;
    std::size_t tested_green_paths = 0;
    SimplificationResult witness;
};

struct BruteForceBudgetState {
    explicit BruteForceBudgetState(long long limit_value) : limit(limit_value) {}

    long long limit = -1;
    std::atomic<long long> used{0};
    std::atomic<bool> exhausted{false};
};

bool take_bruteforce_budget(BruteForceBudgetState* budget) {
    if (budget == nullptr || budget->limit < 0) {
        return true;
    }
    const long long previous = budget->used.fetch_add(1, std::memory_order_relaxed);
    if (previous >= budget->limit) {
        budget->exhausted.store(true, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool bruteforce_budget_exhausted(const BruteForceBudgetState* budget) {
    return budget != nullptr && budget->exhausted.load(std::memory_order_relaxed);
}

bool witness_better_than(const SimplificationResult& candidate, const SimplificationResult& current) {
    if (!current.found) {
        return true;
    }
    if (candidate.crossing_reduction != current.crossing_reduction) {
        return candidate.crossing_reduction > current.crossing_reduction;
    }
    if (candidate.resulting_crossings != current.resulting_crossings) {
        return candidate.resulting_crossings < current.resulting_crossings;
    }
    if (candidate.red_path.size() != current.red_path.size()) {
        return candidate.red_path.size() > current.red_path.size();
    }
    if (candidate.green_path.size() != current.green_path.size()) {
        return candidate.green_path.size() < current.green_path.size();
    }
    return false;
}

bool score_witness_candidate(
    const PDCode& code,
    SimplificationResult& candidate,
    bool require_applicable) {
    try {
        const MidSimplificationApplyResult applied =
            apply_simplification_witness(code, candidate, 0);
        candidate.resulting_crossings = applied.code.size();
        candidate.crossing_reduction =
            static_cast<int>(code.size()) - static_cast<int>(applied.code.size());
        return !require_applicable || candidate.crossing_reduction > 0;
    } catch (const std::exception&) {
        if (require_applicable) {
            return false;
        }
        candidate.resulting_crossings = code.size();
        candidate.crossing_reduction = 0;
        return true;
    }
}

RedPathSearchOutcome search_single_red_path(
    const PDCode& code,
    const Diagram& diagram,
    const DualGraph& base_graph,
    const std::vector<Endpoint>& red_path,
    const SimplifierOptions& options,
    const std::string& path_search_mode,
    int red_index,
    const std::atomic<int>* best_found_index,
    BruteForceBudgetState* brute_budget) {
    check_timeout(options);
    RedPathSearchOutcome outcome;
    outcome.witness.path_search_mode = path_search_mode;
    if (should_skip_parallel_red_path(red_index, best_found_index)) {
        outcome.skipped = true;
        return outcome;
    }
    if (bruteforce_budget_exhausted(brute_budget)) {
        outcome.resource_limited = true;
        return outcome;
    }

    DualGraph graph = base_graph;
    const Endpoint start = red_path.front();
    const Endpoint end = red_path.back();
    const int start_face = graph.edge_to_face[endpoint_key(start)];
    const int start_opposite_face = graph.edge_to_face[endpoint_key(diagram.opposite(start))];
    const int end_face = graph.edge_to_face[endpoint_key(end)];
    const int end_opposite_face = graph.edge_to_face[endpoint_key(diagram.opposite(end))];
    const std::array<int, 2> sources{start_face, start_opposite_face};
    const std::array<int, 2> destinations{end_face, end_opposite_face};

    for (std::size_t i = 1; i + 1 < red_path.size(); ++i) {
        check_timeout(options);
        const Endpoint endpoint = red_path[i];
        const int right_region = graph.edge_to_face[endpoint_key(endpoint)];
        const int left_region = graph.edge_to_face[endpoint_key(diagram.opposite(endpoint))];
        if (GraphEdge* edge = graph.mutable_edge(right_region, left_region)) {
            edge->weight = kBlockedWeight;
        }
    }

    const int cutoff = static_cast<int>(red_path.size()) - 1;
    const bool choose_best_within_red = options.max_paths != -1;

    auto consider_candidate = [&](SimplificationResult& candidate) {
        candidate.path_search_mode = path_search_mode;
        if (!score_witness_candidate(code, candidate, options.require_applicable)) {
            return true;
        }
        candidate.found = true;
        if (witness_better_than(candidate, outcome.witness)) {
            outcome.witness = std::move(candidate);
            outcome.found = true;
        }
        if (!choose_best_within_red) {
            outcome.completed = true;
            outcome.witness.tested_green_paths = outcome.tested_green_paths;
            return false;
        }
        return true;
    };

    auto test_green_path = [&](const std::vector<int>& green_path) {
        check_timeout(options);
        if (should_skip_parallel_red_path(red_index, best_found_index)) {
            outcome.skipped = true;
            return false;
        }
        if (!take_bruteforce_budget(brute_budget)) {
            outcome.resource_limited = true;
            return false;
        }
        ++outcome.tested_green_paths;
        if (green_path.size() >= red_path.size()) {
            return true;
        }
        SimplificationResult candidate;
        candidate.path_search_mode = path_search_mode;
        if (do_check(
                diagram,
                graph,
                red_path,
                green_path,
                Direction::Left,
                candidate,
                options)) {
            if (!consider_candidate(candidate)) {
                return false;
            }
        }
        candidate = SimplificationResult();
        candidate.path_search_mode = path_search_mode;
        if (do_check(
                diagram,
                graph,
                red_path,
                green_path,
                Direction::Right,
                candidate,
                options)) {
            if (!consider_candidate(candidate)) {
                return false;
            }
        }
        return true;
    };

    for (int source : sources) {
        for (int destination : destinations) {
            check_timeout(options);
            if (should_skip_parallel_red_path(red_index, best_found_index)) {
                outcome.skipped = true;
                return outcome;
            }
            if (options.max_paths == -1 && !options.ban_heuristic) {
                const std::vector<std::vector<int>> found_paths =
                    collect_heuristic_paths(graph, source, destination, cutoff, options);
                for (const auto& green_path : found_paths) {
                    if (!test_green_path(green_path)) {
                        return outcome;
                    }
                }
            } else {
                std::size_t visited_for_pair = 0;
                const bool completed = visit_simple_paths(
                    graph,
                    source,
                    destination,
                    cutoff,
                    options,
                    [&](const std::vector<int>& green_path) {
                        if (options.max_paths != -1 && visited_for_pair > static_cast<std::size_t>(options.max_paths)) {
                            return false;
                        }
                        ++visited_for_pair;
                        return test_green_path(green_path);
                    });
                if (!completed || outcome.found || outcome.skipped || outcome.resource_limited) {
                    return outcome;
                }
            }
        }
    }

    outcome.completed = true;
    outcome.witness.tested_green_paths = outcome.tested_green_paths;
    if (outcome.found) {
        outcome.witness.tested_green_paths = outcome.tested_green_paths;
    }
    return outcome;
}

SimplificationResult merge_red_path_outcomes(
    const std::vector<RedPathSearchOutcome>& outcomes,
    const std::string& path_search_mode) {
    SimplificationResult result;
    result.path_search_mode = path_search_mode;

    int first_found = -1;
    for (int i = 0; i < static_cast<int>(outcomes.size()); ++i) {
        if (outcomes[i].found) {
            first_found = i;
            break;
        }
    }

    const int limit = first_found >= 0 ? first_found : static_cast<int>(outcomes.size()) - 1;
    for (int i = 0; i <= limit; ++i) {
        if (outcomes[i].resource_limited) {
            result.resource_limited = true;
        }
        if (!outcomes[i].completed && !outcomes[i].found && !outcomes[i].resource_limited && !result.resource_limited) {
            std::ostringstream message;
            message << "Parallel brute-force search did not complete red path " << i;
            throw std::runtime_error(message.str());
        }
        if (outcomes[i].completed || outcomes[i].found || outcomes[i].resource_limited ||
            outcomes[i].tested_green_paths > 0) {
            ++result.tested_red_paths;
        }
        result.tested_green_paths += outcomes[i].tested_green_paths;
    }

    if (first_found >= 0) {
        result = outcomes[first_found].witness;
        result.path_search_mode = path_search_mode;
        result.tested_red_paths = static_cast<std::size_t>(first_found + 1);
        result.tested_green_paths = 0;
        for (int i = 0; i <= first_found; ++i) {
            result.tested_green_paths += outcomes[i].tested_green_paths;
        }
        for (int i = 0; i <= first_found; ++i) {
            result.resource_limited = result.resource_limited || outcomes[i].resource_limited;
        }
    }
    return result;
}

SimplificationResult merge_best_red_path_batch(
    const std::vector<RedPathSearchOutcome>& outcomes,
    const std::string& path_search_mode) {
    SimplificationResult result;
    result.path_search_mode = path_search_mode;

    int best_found = -1;
    for (int i = 0; i < static_cast<int>(outcomes.size()); ++i) {
        const RedPathSearchOutcome& outcome = outcomes[i];
        if (outcome.resource_limited) {
            result.resource_limited = true;
        }
        if (!outcome.completed && !outcome.found && !outcome.resource_limited && !outcome.skipped) {
            std::ostringstream message;
            message << "Parallel heuristic search did not complete red path batch entry " << i;
            throw std::runtime_error(message.str());
        }
        if (outcome.completed || outcome.found || outcome.resource_limited ||
            outcome.tested_green_paths > 0) {
            ++result.tested_red_paths;
        }
        result.tested_green_paths += outcome.tested_green_paths;
        if (outcome.found &&
            (best_found < 0 ||
             witness_better_than(outcome.witness, outcomes[best_found].witness))) {
            best_found = i;
        }
    }

    if (best_found >= 0) {
        result = outcomes[best_found].witness;
        result.path_search_mode = path_search_mode;
        std::size_t tested_red_paths = 0;
        std::size_t tested_green_paths = 0;
        bool resource_limited = false;
        for (const RedPathSearchOutcome& outcome : outcomes) {
            if (outcome.completed || outcome.found || outcome.resource_limited ||
                outcome.tested_green_paths > 0) {
                ++tested_red_paths;
            }
            tested_green_paths += outcome.tested_green_paths;
            resource_limited = resource_limited || outcome.resource_limited;
        }
        result.tested_red_paths = tested_red_paths;
        result.tested_green_paths = tested_green_paths;
        result.resource_limited = resource_limited;
    }
    return result;
}

SimplificationResult find_simplification_parallel_bruteforce(
    const PDCode& code,
    const Diagram& diagram,
    const DualGraph& base_graph,
    const std::vector<std::vector<Endpoint>>& red_lines,
    const SimplifierOptions& options,
    const std::string& path_search_mode,
    int worker_count,
    BruteForceBudgetState* brute_budget) {
    std::vector<RedPathSearchOutcome> outcomes(red_lines.size());
    std::atomic<int> next_index(0);
    std::atomic<int> best_found_index(static_cast<int>(red_lines.size()));
    std::atomic<bool> failed(false);
    std::exception_ptr first_exception;
    std::mutex exception_mutex;

    auto worker = [&]() {
        while (!failed.load(std::memory_order_relaxed)) {
            if (next_index.load(std::memory_order_relaxed) >=
                best_found_index.load(std::memory_order_relaxed)) {
                break;
            }
            const int index = next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= static_cast<int>(red_lines.size())) {
                break;
            }
            if (should_skip_parallel_red_path(index, &best_found_index)) {
                outcomes[index].skipped = true;
                continue;
            }
            try {
                RedPathSearchOutcome outcome = search_single_red_path(
                    code,
                    diagram,
                    base_graph,
                    red_lines[index],
                    options,
                    path_search_mode,
                    index,
                    &best_found_index,
                    brute_budget);
                if (outcome.found) {
                    record_parallel_found_index(index, &best_found_index);
                }
                outcomes[index] = std::move(outcome);
            } catch (...) {
                failed.store(true, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(exception_mutex);
                if (!first_exception) {
                    first_exception = std::current_exception();
                }
                break;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back(worker);
    }
    for (std::thread& thread : workers) {
        thread.join();
    }
    if (first_exception) {
        std::rethrow_exception(first_exception);
    }

    return merge_red_path_outcomes(outcomes, path_search_mode);
}

SimplificationResult find_simplification_parallel_heuristic_batches(
    const PDCode& code,
    const Diagram& diagram,
    const DualGraph& base_graph,
    const std::vector<std::vector<Endpoint>>& red_lines,
    const SimplifierOptions& options,
    const std::string& path_search_mode,
    int worker_count) {
    SimplificationResult aggregate;
    aggregate.path_search_mode = path_search_mode;
    if (red_lines.empty()) {
        return aggregate;
    }
    SimplificationResult best_witness;
    best_witness.path_search_mode = path_search_mode;
    int batches_since_best = 0;

    for (std::size_t batch_begin = 0; batch_begin < red_lines.size();
         batch_begin += static_cast<std::size_t>(worker_count)) {
        check_timeout(options);
        const std::size_t batch_end = std::min(
            red_lines.size(),
            batch_begin + static_cast<std::size_t>(worker_count));
        std::vector<RedPathSearchOutcome> outcomes(batch_end - batch_begin);
        std::atomic<bool> failed(false);
        std::exception_ptr first_exception;
        std::mutex exception_mutex;

        auto worker = [&](std::size_t local_index) {
            const std::size_t red_index = batch_begin + local_index;
            try {
                outcomes[local_index] = search_single_red_path(
                    code,
                    diagram,
                    base_graph,
                    red_lines[red_index],
                    options,
                    path_search_mode,
                    -1,
                    nullptr,
                    nullptr);
            } catch (...) {
                failed.store(true, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(exception_mutex);
                if (!first_exception) {
                    first_exception = std::current_exception();
                }
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(batch_end - batch_begin);
        for (std::size_t local_index = 0; local_index < batch_end - batch_begin; ++local_index) {
            workers.emplace_back(worker, local_index);
        }
        for (std::thread& thread : workers) {
            thread.join();
        }
        if (first_exception) {
            std::rethrow_exception(first_exception);
        }
        if (failed.load(std::memory_order_relaxed)) {
            throw std::runtime_error("Parallel heuristic search failed");
        }

        SimplificationResult batch = merge_best_red_path_batch(outcomes, path_search_mode);
        aggregate.tested_red_paths += batch.tested_red_paths;
        aggregate.tested_green_paths += batch.tested_green_paths;
        aggregate.resource_limited = aggregate.resource_limited || batch.resource_limited;
        if (batch.found) {
            if (witness_better_than(batch, best_witness)) {
                best_witness = batch;
                batches_since_best = 0;
            } else if (best_witness.found) {
                ++batches_since_best;
            }
        } else if (best_witness.found) {
            ++batches_since_best;
        }
        if (best_witness.found &&
            (batches_since_best >= kHeuristicBestLookaheadBatches ||
             batch_end == red_lines.size() ||
             aggregate.resource_limited)) {
            best_witness.tested_red_paths = aggregate.tested_red_paths;
            best_witness.tested_green_paths = aggregate.tested_green_paths;
            best_witness.resource_limited = aggregate.resource_limited;
            return best_witness;
        }
        if (aggregate.resource_limited) {
            return aggregate;
        }
    }
    return aggregate;
}

}  // namespace

inline SimplificationResult find_simplification(
    const PDCode& code,
    const SimplifierOptions& options) {
    const SimplifierOptions run_options = with_timeout_deadline(options);
    check_timeout(run_options);
    SimplificationResult result;
    result.path_search_mode = search_mode_for_options(run_options);
    Diagram diagram(code);
    check_timeout(run_options);
    DualGraph base_graph(diagram);
    check_timeout(run_options);
    auto red_lines = possible_red_lines(diagram);
    const bool heuristic_mode = run_options.max_paths == -1 && !run_options.ban_heuristic;
    const bool heuristic_parallel_enabled =
        heuristic_mode && run_options.force_heuristic_parallel;
    const bool red_path_parallel_candidate =
        run_options.max_paths == -1 && (!heuristic_mode || heuristic_parallel_enabled);
    const int worker_count = red_path_parallel_candidate
        ? selected_bruteforce_worker_count(
              run_options.max_threads,
              static_cast<int>(red_lines.size()))
        : 1;
    if (heuristic_mode) {
        std::ostringstream message;
        if (worker_count > 1) {
            message << "heuristic_parallel_batches first_hit=no"
                    << " lookahead_batches=" << kHeuristicBestLookaheadBatches
                    << " red_paths=" << red_lines.size();
        } else {
            message << "heuristic_legacy first_hit=yes red_paths=" << red_lines.size();
        }
        emit_progress(run_options, message.str());
    }
    check_timeout(run_options);
    const bool brute_force_mode = red_path_parallel_candidate && run_options.ban_heuristic;
    BruteForceBudgetState brute_budget(run_options.bruteforce_budget);
    BruteForceBudgetState* brute_budget_ptr = brute_force_mode ? &brute_budget : nullptr;
    if (red_path_parallel_candidate && run_options.max_threads == -1) {
        std::ostringstream message;
        message << result.path_search_mode << "_threads max_thread=-1"
                << " actual_threads=" << worker_count
                << " red_paths=" << red_lines.size()
                << " bruteforce_budget=" << run_options.bruteforce_budget;
        emit_progress(run_options, message.str());
    } else if (red_path_parallel_candidate && run_options.verbose && worker_count > 1) {
        std::ostringstream message;
        message << result.path_search_mode << "_threads max_thread="
                << run_options.max_threads
                << " actual_threads=" << worker_count
                << " red_paths=" << red_lines.size()
                << " bruteforce_budget=" << run_options.bruteforce_budget;
        emit_progress(run_options, message.str());
    }
    if (worker_count > 1) {
        if (heuristic_mode) {
            return find_simplification_parallel_heuristic_batches(
                code,
                diagram,
                base_graph,
                red_lines,
                run_options,
                result.path_search_mode,
                worker_count);
        }
        return find_simplification_parallel_bruteforce(
            code,
            diagram,
            base_graph,
            red_lines,
            run_options,
            result.path_search_mode,
            worker_count,
            brute_budget_ptr);
    }

    for (std::size_t red_index = 0; red_index < red_lines.size(); ++red_index) {
        const auto& red_path = red_lines[red_index];
        check_timeout(run_options);
        if (run_options.verbose && (red_index == 0 || red_index % 1024 == 0)) {
            std::ostringstream message;
            message << "search_progress mode=" << result.path_search_mode
                    << " red_index=" << red_index
                    << " red_paths=" << red_lines.size()
                    << " red_length=" << red_path.size()
                    << " tested_green=" << result.tested_green_paths;
            emit_progress(run_options, message.str());
        }
        ++result.tested_red_paths;
        const RedPathSearchOutcome outcome = search_single_red_path(
            code,
            diagram,
            base_graph,
            red_path,
            run_options,
            result.path_search_mode,
            -1,
            nullptr,
            brute_budget_ptr);
        result.tested_green_paths += outcome.tested_green_paths;
        result.resource_limited = result.resource_limited || outcome.resource_limited;
        if (outcome.resource_limited) {
            return result;
        }
        if (outcome.found) {
            SimplificationResult found = outcome.witness;
            found.path_search_mode = result.path_search_mode;
            found.tested_red_paths = result.tested_red_paths;
            found.tested_green_paths = result.tested_green_paths;
            found.resource_limited = outcome.resource_limited;
            return found;
        }
    }

    return result;
}

enum class AdaptiveStage {
    R3Prepass,
    HeuristicSearch,
    NonMonotone
};

struct AdaptiveStageStats {
    int successes = 0;
    int misses = 0;
    int timeouts = 0;
    int consecutive_successes = 0;
    int consecutive_misses = 0;
    int consecutive_timeouts = 0;
};

struct AdaptiveScheduler {
    AdaptiveStageStats r3_prepass;
    AdaptiveStageStats heuristic_search;
    AdaptiveStageStats non_monotone;
};

inline const char* adaptive_stage_name(AdaptiveStage stage) {
    switch (stage) {
        case AdaptiveStage::R3Prepass:
            return "r3_prepass";
        case AdaptiveStage::HeuristicSearch:
            return "heuristic_search";
        case AdaptiveStage::NonMonotone:
            return "non_monotone";
    }
    return "unknown";
}

inline int adaptive_stage_base_order(AdaptiveStage stage) {
    switch (stage) {
        case AdaptiveStage::R3Prepass:
            return 0;
        case AdaptiveStage::HeuristicSearch:
            return 1;
        case AdaptiveStage::NonMonotone:
            return 2;
    }
    return 3;
}

inline AdaptiveStageStats& adaptive_stage_stats(AdaptiveScheduler& scheduler, AdaptiveStage stage) {
    switch (stage) {
        case AdaptiveStage::R3Prepass:
            return scheduler.r3_prepass;
        case AdaptiveStage::HeuristicSearch:
            return scheduler.heuristic_search;
        case AdaptiveStage::NonMonotone:
            return scheduler.non_monotone;
    }
    return scheduler.non_monotone;
}

inline const AdaptiveStageStats& adaptive_stage_stats(
    const AdaptiveScheduler& scheduler,
    AdaptiveStage stage) {
    switch (stage) {
        case AdaptiveStage::R3Prepass:
            return scheduler.r3_prepass;
        case AdaptiveStage::HeuristicSearch:
            return scheduler.heuristic_search;
        case AdaptiveStage::NonMonotone:
            return scheduler.non_monotone;
    }
    return scheduler.non_monotone;
}

inline int adaptive_stage_base_score(AdaptiveStage stage) {
    switch (stage) {
        case AdaptiveStage::R3Prepass:
            return 300;
        case AdaptiveStage::HeuristicSearch:
            return 240;
        case AdaptiveStage::NonMonotone:
            return 120;
    }
    return 0;
}

inline int adaptive_stage_score(const AdaptiveScheduler& scheduler, AdaptiveStage stage) {
    const AdaptiveStageStats& stats = adaptive_stage_stats(scheduler, stage);
    return adaptive_stage_base_score(stage)
        + stats.successes * 70
        + stats.consecutive_successes * 180
        - stats.misses * 35
        - stats.consecutive_misses * 45
        - stats.timeouts * 140
        - stats.consecutive_timeouts * 260;
}

inline void record_adaptive_stage_success(AdaptiveScheduler& scheduler, AdaptiveStage stage) {
    AdaptiveStageStats& stats = adaptive_stage_stats(scheduler, stage);
    ++stats.successes;
    ++stats.consecutive_successes;
    stats.consecutive_misses = 0;
    stats.consecutive_timeouts = 0;
}

inline void record_adaptive_stage_miss(AdaptiveScheduler& scheduler, AdaptiveStage stage) {
    AdaptiveStageStats& stats = adaptive_stage_stats(scheduler, stage);
    ++stats.misses;
    ++stats.consecutive_misses;
    stats.consecutive_successes = 0;
    stats.consecutive_timeouts = 0;
}

inline void record_adaptive_stage_timeout(AdaptiveScheduler& scheduler, AdaptiveStage stage) {
    AdaptiveStageStats& stats = adaptive_stage_stats(scheduler, stage);
    ++stats.timeouts;
    ++stats.consecutive_timeouts;
    ++stats.consecutive_misses;
    stats.consecutive_successes = 0;
}

inline std::vector<AdaptiveStage> adaptive_stage_order(const AdaptiveScheduler& scheduler) {
    std::vector<AdaptiveStage> stages{
        AdaptiveStage::R3Prepass,
        AdaptiveStage::HeuristicSearch,
        AdaptiveStage::NonMonotone};
    std::stable_sort(stages.begin(), stages.end(), [&](AdaptiveStage lhs, AdaptiveStage rhs) {
        const int lhs_score = adaptive_stage_score(scheduler, lhs);
        const int rhs_score = adaptive_stage_score(scheduler, rhs);
        if (lhs_score != rhs_score) {
            return lhs_score > rhs_score;
        }
        return adaptive_stage_base_order(lhs) < adaptive_stage_base_order(rhs);
    });
    return stages;
}

inline void emit_adaptive_scheduler_state(
    const SimplifierOptions& options,
    int round,
    const AdaptiveScheduler& scheduler,
    const std::vector<AdaptiveStage>& order) {
    std::ostringstream message;
    message << "round " << round << " adaptive_order";
    for (AdaptiveStage stage : order) {
        const AdaptiveStageStats& stats = adaptive_stage_stats(scheduler, stage);
        message << ' ' << adaptive_stage_name(stage)
                << "(score=" << adaptive_stage_score(scheduler, stage)
                << ",successes=" << stats.successes
                << ",misses=" << stats.misses
                << ",timeouts=" << stats.timeouts
                << ",success_streak=" << stats.consecutive_successes
                << ",miss_streak=" << stats.consecutive_misses
                << ",timeout_streak=" << stats.consecutive_timeouts
                << ')';
    }
    emit_progress(options, message.str());
}

inline ReductionResult reduce_pd_code(
    const PDCode& code,
    std::size_t known_crossingless_components,
    const SimplifierOptions& options,
    int reduction_round) {
    const SimplifierOptions run_options = with_timeout_deadline(options);
    ReductionResult output;
    output.code = code;
    output.crossingless_components = known_crossingless_components;
    PDCode best_code = output.code;
    std::size_t best_crossingless_components = output.crossingless_components;
    auto stop_if_quit_at_crossing = [&](const std::string& stage) {
        if (run_options.quit_at_crossing < 0 ||
            output.code.size() > static_cast<std::size_t>(run_options.quit_at_crossing)) {
            return false;
        }
        output.stopped_by_crossing_limit = true;
        std::ostringstream message;
        message << stage << " stop_quit_at_crossing threshold="
                << run_options.quit_at_crossing
                << " crossings=" << output.code.size()
                << " crossingless_components=" << output.crossingless_components;
        emit_progress(run_options, message.str());
        return true;
    };

    try {
        check_timeout(run_options);
        const bool top_level_heuristic_mode =
            run_options.max_paths == -1 && !run_options.ban_heuristic;
        bool big_heuristic_mode = false;
        bool efficient_phase_timed_out = false;
        const auto efficient_phase_deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(kEfficientPhaseTimeSliceSeconds);
        auto efficient_phase_active = [&]() {
            return top_level_heuristic_mode && !big_heuristic_mode;
        };
        auto efficient_phase_expired = [&]() {
            return efficient_phase_active() &&
                   std::chrono::steady_clock::now() >= efficient_phase_deadline;
        };
        auto with_efficient_phase_deadline = [&](SimplifierOptions phase_options) {
            if (!efficient_phase_active()) {
                return phase_options;
            }
            if (!phase_options.has_timeout_deadline ||
                efficient_phase_deadline < phase_options.timeout_deadline) {
                phase_options.has_timeout_deadline = true;
                phase_options.timeout_deadline = efficient_phase_deadline;
                phase_options.timeout_seconds = kEfficientPhaseTimeSliceSeconds;
            }
            return phase_options;
        };
        auto switch_to_big_heuristic = [&](const std::string& stage) {
            if (!efficient_phase_active()) {
                return;
            }
            if (best_code.size() < output.code.size()) {
                output.code = best_code;
                output.crossingless_components = best_crossingless_components;
            }
            output.code = canonical_output_code(output.code);
            big_heuristic_mode = true;
            efficient_phase_timed_out = false;
            std::ostringstream message;
            message << stage << " efficient_phase_timeout seconds="
                    << kEfficientPhaseTimeSliceSeconds
                    << " handoff=big_heuristic crossings=" << output.code.size()
                    << " crossingless_components=" << output.crossingless_components;
            emit_progress(run_options, message.str());
        };
        {
            std::ostringstream message;
            message << "start input_crossings=" << code.size()
                    << " known_crossingless_components=" << known_crossingless_components
                    << " reduction_round=" << reduction_round
                    << " max_paths=" << run_options.max_paths
                    << " max_thread=" << run_options.max_threads
                    << " bruteforce_budget=" << run_options.bruteforce_budget
                    << " timeout=" << run_options.timeout_seconds
                    << " quit_at_crossing=" << run_options.quit_at_crossing
                    << " heuristic=" << (run_options.ban_heuristic ? "off" : "on")
                    << " efficient_phase_seconds="
                    << (top_level_heuristic_mode ? kEfficientPhaseTimeSliceSeconds : -1)
                    << " reapr=" << (run_options.enable_reapr ? "on" : "off")
                    << " reapr_retry_max=" << run_options.reapr_retry_max;
            emit_progress(run_options, message.str());
        }

        const PDSimplificationResult prepared =
            simplify_pd_code_checked(
                code,
                known_crossingless_components,
                &run_options);
        output.code = canonical_output_code(prepared.code);
        output.crossingless_components = prepared.crossingless_components;
        output.reidemeister_i_moves = prepared.reidemeister_i_moves;
        output.reidemeister_ii_moves = prepared.reidemeister_ii_moves;
        output.nugatory_crossing_moves = prepared.nugatory_crossing_moves;
        std::set<std::string> seen_reduction_states;
        seen_reduction_states.insert(format_final_pd_code(output.code));
        AdaptiveScheduler adaptive_scheduler;
        best_code = output.code;
        best_crossingless_components = output.crossingless_components;
        check_timeout(run_options);
        {
            std::ostringstream message;
            message << "pre_simplify input_crossings=" << code.size()
                    << " output_crossings=" << output.code.size()
                    << " crossingless_components=" << output.crossingless_components
                    << " r1_moves=" << prepared.reidemeister_i_moves
                    << " r2_moves=" << prepared.reidemeister_ii_moves
                    << " nugatory_moves=" << prepared.nugatory_crossing_moves;
            emit_progress(run_options, message.str());
        }

        (void)stop_if_quit_at_crossing("pre_simplify");

        if (!output.stopped_by_crossing_limit && run_options.enable_reapr) {
            {
                std::ostringstream message;
                message << "reapr_oracle_start crossings=" << output.code.size()
                        << " crossingless_components=" << output.crossingless_components;
                emit_progress(run_options, message.str());
            }
            const std::size_t before_reapr_crossings = output.code.size();
            const ReaprOracleResult reapr =
                try_reapr_oracle(output.code, output.crossingless_components, run_options);
            output.alexander_determinant_before = reapr.determinant_before;
            output.alexander_determinant_after = reapr.determinant_after;
            output.reapr_invariants_before = reapr.invariants_before;
            output.reapr_invariants_after = reapr.invariants_after;
            output.reapr_status = reapr.status;
            output.reapr_rejected = reapr.rejected;
            output.reapr_attempts = reapr.attempts;
            if (reapr.accepted) {
                output.reapr_used = true;
                output.reapr_rounds += 1;
                output.reapr_warning = kReaprWarning;
                output.code = canonical_output_code(reapr.code);
                output.crossingless_components = reapr.crossingless_components;
                if (reapr.matched_step_codes.empty()) {
                    emit_step_pd(run_options, 0, output.code);
                } else {
                    for (const PDCode& step_code : reapr.matched_step_codes) {
                        emit_step_pd(run_options, 0, step_code);
                    }
                }
                check_timeout(run_options);
                const PDSimplificationResult reapr_cleanup =
                    simplify_pd_code_checked(output.code, output.crossingless_components, &run_options);
                output.code = canonical_output_code(reapr_cleanup.code);
                output.crossingless_components = reapr_cleanup.crossingless_components;
                output.reidemeister_i_moves += reapr_cleanup.reidemeister_i_moves;
                output.reidemeister_ii_moves += reapr_cleanup.reidemeister_ii_moves;
                output.nugatory_crossing_moves += reapr_cleanup.nugatory_crossing_moves;
                best_code = output.code;
                best_crossingless_components = output.crossingless_components;
                (void)stop_if_quit_at_crossing("reapr_oracle");
            }
            {
                std::ostringstream message;
                message << "reapr_oracle_done status=" << output.reapr_status
                        << " accepted=" << (output.reapr_used ? "yes" : "no")
                        << " attempts=" << output.reapr_attempts
                        << " input_crossings=" << before_reapr_crossings
                        << " output_crossings=" << output.code.size()
                        << " determinant_before=" << output.alexander_determinant_before
                        << " determinant_after=" << output.alexander_determinant_after
                        << " invariants_before=\"" << output.reapr_invariants_before
                        << "\" invariants_after=\"" << output.reapr_invariants_after << "\"";
                emit_progress(run_options, message.str());
            }
        }

        while (!output.stopped_by_crossing_limit &&
               (reduction_round < 0 || output.mid_simplification_rounds < reduction_round)) {
            check_timeout(run_options);
            const int round = output.mid_simplification_rounds + 1;
            const bool adaptive_mode = run_options.max_paths == -1 && !run_options.ban_heuristic;
            SimplificationResult search;
            bool restart_round = false;
            if (efficient_phase_expired()) {
                switch_to_big_heuristic("round " + std::to_string(round));
            }

            auto run_r3_prepass = [&]() {
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " r3_prepass_start crossings=" << output.code.size()
                            << " max_depth=" << kR3PrepassMaxDepth
                            << " max_states=" << kR3PrepassMaxStates;
                    emit_progress(run_options, message.str());
                }
                ReidemeisterIIIFailoverResult r3_prepass;
                bool stage_timeout = false;
                bool stage_error = false;
                try {
                    const SimplifierOptions r3_options =
                        with_time_slice(run_options, kR3PrepassTimeSliceSeconds);
                    r3_prepass = find_reidemeister_iii_failover(
                        output.code,
                        output.crossingless_components,
                        r3_options,
                        kR3PrepassMaxDepth,
                        kR3PrepassMaxStates);
                } catch (const TimeoutError&) {
                    if (timeout_expired(run_options)) {
                        throw;
                    }
                    stage_timeout = true;
                    std::ostringstream timeout_message;
                    timeout_message << "round " << round
                                    << " r3_prepass_timeout seconds="
                                    << kR3PrepassTimeSliceSeconds;
                    emit_progress(run_options, timeout_message.str());
                } catch (const std::exception& error) {
                    stage_error = true;
                    std::ostringstream error_message;
                    error_message << "round " << round
                                  << " r3_prepass_error message=\""
                                  << error.what() << "\"";
                    emit_progress(run_options, error_message.str());
                }
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " r3_prepass_done found=" << (r3_prepass.found ? "yes" : "no")
                            << " depth=" << r3_prepass.depth
                            << " visited_states=" << r3_prepass.visited_states
                            << " final_crossings="
                            << (r3_prepass.found ? r3_prepass.code.size() : output.code.size())
                            << " r1_moves=" << r3_prepass.reidemeister_i_moves
                            << " r2_moves=" << r3_prepass.reidemeister_ii_moves
                            << " r3_moves=" << r3_prepass.reidemeister_iii_moves
                            << " nugatory_moves=" << r3_prepass.nugatory_crossing_moves;
                    emit_progress(run_options, message.str());
                }
                if (stage_timeout) {
                    record_adaptive_stage_timeout(adaptive_scheduler, AdaptiveStage::R3Prepass);
                    return false;
                }
                if (stage_error) {
                    record_adaptive_stage_miss(adaptive_scheduler, AdaptiveStage::R3Prepass);
                    return false;
                }
                if (r3_prepass.found) {
                    record_adaptive_stage_success(adaptive_scheduler, AdaptiveStage::R3Prepass);
                    output.code = canonical_output_code(r3_prepass.code);
                    output.crossingless_components = r3_prepass.crossingless_components;
                    seen_reduction_states.insert(format_final_pd_code(output.code));
                    if (output.code.size() < best_code.size()) {
                        best_code = output.code;
                        best_crossingless_components = output.crossingless_components;
                    }
                    output.reidemeister_i_moves += r3_prepass.reidemeister_i_moves;
                    output.reidemeister_ii_moves += r3_prepass.reidemeister_ii_moves;
                    output.reidemeister_iii_moves += r3_prepass.reidemeister_iii_moves;
                    output.nugatory_crossing_moves += r3_prepass.nugatory_crossing_moves;
                    if (stop_if_quit_at_crossing("r3_prepass")) {
                        restart_round = true;
                        return true;
                    }
                    restart_round = true;
                    return true;
                }
                record_adaptive_stage_miss(adaptive_scheduler, AdaptiveStage::R3Prepass);
                return false;
            };

            auto run_heuristic_search = [&](bool use_soft_slice) {
                SimplifierOptions search_options = run_options;
                search_options.require_applicable = true;
                search_options.force_heuristic_parallel = big_heuristic_mode;
                output.last_path_search_mode = search_mode_for_options(search_options);
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " search_start crossings=" << output.code.size()
                            << " mode=" << output.last_path_search_mode
                            << " max_thread=" << search_options.max_threads
                            << " big_heuristic=" << (big_heuristic_mode ? "yes" : "no")
                            << " soft_slice="
                            << (use_soft_slice ? kMidSearchTimeSliceSeconds : -1);
                    emit_progress(run_options, message.str());
                }
                bool stage_timeout = false;
                try {
                    SimplifierOptions sliced_search_options = search_options;
                    if (adaptive_mode && use_soft_slice) {
                        sliced_search_options =
                            with_time_slice(search_options, kMidSearchTimeSliceSeconds);
                    }
                    sliced_search_options =
                        with_efficient_phase_deadline(sliced_search_options);
                    search = find_simplification(output.code, sliced_search_options);
                } catch (const TimeoutError&) {
                    if (efficient_phase_expired() && !timeout_expired(run_options)) {
                        efficient_phase_timed_out = true;
                        search.path_search_mode = search_mode_for_options(search_options);
                        std::ostringstream timeout_message;
                        timeout_message << "round " << round
                                        << " efficient_phase_search_timeout seconds="
                                        << kEfficientPhaseTimeSliceSeconds;
                        emit_progress(run_options, timeout_message.str());
                    } else
                    if (!use_soft_slice || timeout_expired(run_options)) {
                        throw;
                    } else {
                        stage_timeout = true;
                        search.path_search_mode = search_mode_for_options(search_options);
                        std::ostringstream timeout_message;
                        timeout_message << "round " << round
                                        << " search_timeout seconds="
                                        << kMidSearchTimeSliceSeconds;
                        emit_progress(run_options, timeout_message.str());
                    }
                }
                output.tested_red_paths += search.tested_red_paths;
                output.tested_green_paths += search.tested_green_paths;
                output.last_path_search_mode = search.path_search_mode;
                output.resource_limited = output.resource_limited || search.resource_limited;
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " search_done found=" << (search.found ? "yes" : "no")
                            << " mode=" << search.path_search_mode
                            << " tested_red=" << search.tested_red_paths
                            << " tested_green=" << search.tested_green_paths
                            << " resource_limited=" << (search.resource_limited ? "yes" : "no");
                    emit_progress(run_options, message.str());
                }
                if (adaptive_mode) {
                    if (stage_timeout) {
                        record_adaptive_stage_timeout(
                            adaptive_scheduler,
                            AdaptiveStage::HeuristicSearch);
                    } else if (search.found) {
                        record_adaptive_stage_success(
                            adaptive_scheduler,
                            AdaptiveStage::HeuristicSearch);
                    } else {
                        record_adaptive_stage_miss(
                            adaptive_scheduler,
                            AdaptiveStage::HeuristicSearch);
                    }
                }
                return search.found;
            };

            auto run_non_monotone = [&]() {
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " non_monotone_start crossings=" << output.code.size()
                            << " max_depth=" << kNonMonotoneMaxDepth
                            << " beam_width=" << kNonMonotoneBeamWidth
                            << " max_red_length=" << kNonMonotoneMaxRedLength
                            << " max_total_green_tests=" << kNonMonotoneMaxTotalGreenTests;
                    emit_progress(run_options, message.str());
                }
                NonMonotoneSearchResult non_monotone;
                bool stage_timeout = false;
                bool stage_error = false;
                try {
                    const SimplifierOptions non_monotone_options =
                        with_time_slice(run_options, kNonMonotoneTimeSliceSeconds);
                    non_monotone = find_non_monotone_reduction(
                        output.code,
                        output.crossingless_components,
                        non_monotone_options);
                } catch (const TimeoutError&) {
                    if (timeout_expired(run_options)) {
                        throw;
                    }
                    stage_timeout = true;
                    std::ostringstream timeout_message;
                    timeout_message << "round " << round
                                    << " non_monotone_timeout seconds="
                                    << kNonMonotoneTimeSliceSeconds;
                    emit_progress(run_options, timeout_message.str());
                } catch (const std::exception& error) {
                    stage_error = true;
                    std::ostringstream error_message;
                    error_message << "round " << round
                                  << " non_monotone_error message=\""
                                  << error.what() << "\"";
                    emit_progress(run_options, error_message.str());
                }
                output.tested_red_paths += non_monotone.tested_red_paths;
                output.tested_green_paths += non_monotone.tested_green_paths;
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " non_monotone_done found=" << (non_monotone.found ? "yes" : "no")
                            << " depth=" << non_monotone.depth
                            << " steps=" << non_monotone.steps.size()
                            << " tested_red=" << non_monotone.tested_red_paths
                            << " tested_green=" << non_monotone.tested_green_paths
                            << " applied_candidates=" << non_monotone.applied_candidates
                            << " generated_states=" << non_monotone.generated_states
                            << " final_crossings="
                            << (non_monotone.found ? non_monotone.code.size() : output.code.size())
                            << " r1_moves=" << non_monotone.reidemeister_i_moves
                            << " r2_moves=" << non_monotone.reidemeister_ii_moves
                            << " r3_moves=" << non_monotone.reidemeister_iii_moves
                            << " nugatory_moves=" << non_monotone.nugatory_crossing_moves;
                    emit_progress(run_options, message.str());
                }
                if (stage_timeout) {
                    record_adaptive_stage_timeout(adaptive_scheduler, AdaptiveStage::NonMonotone);
                    return false;
                }
                if (stage_error) {
                    record_adaptive_stage_miss(adaptive_scheduler, AdaptiveStage::NonMonotone);
                    return false;
                }
                if (non_monotone.found) {
                    record_adaptive_stage_success(adaptive_scheduler, AdaptiveStage::NonMonotone);
                    for (const NonMonotoneStep& step : non_monotone.steps) {
                        if (reduction_round >= 0 &&
                            output.mid_simplification_rounds >= reduction_round) {
                            break;
                        }
                        const int step_round = output.mid_simplification_rounds + 1;
                        const std::size_t before_step_crossings = output.code.size();
                        ++output.mid_simplification_rounds;
                        output.code = canonical_output_code(step.code);
                        output.crossingless_components = step.crossingless_components;
                        output.reidemeister_i_moves += step.reidemeister_i_moves;
                        output.reidemeister_ii_moves += step.reidemeister_ii_moves;
                        output.reidemeister_iii_moves += step.reidemeister_iii_moves;
                        output.nugatory_crossing_moves += step.nugatory_crossing_moves;
                        seen_reduction_states.insert(format_final_pd_code(output.code));
                        if (output.code.size() < best_code.size()) {
                            best_code = output.code;
                            best_crossingless_components = output.crossingless_components;
                        }
                        emit_step_pd(run_options, step_round, output.code);
                        {
                            std::ostringstream message;
                            message << "round " << step_round
                                    << " non_monotone_applied kind=" << step.kind
                                    << " crossings=" << before_step_crossings
                                    << " -> " << output.code.size()
                                    << " red_length=" << step.red_length
                                    << " green_length=" << step.green_length
                                    << " crossingless_components=" << output.crossingless_components
                                    << " r1_moves=" << step.reidemeister_i_moves
                                    << " r2_moves=" << step.reidemeister_ii_moves
                                    << " r3_moves=" << step.reidemeister_iii_moves
                                    << " nugatory_moves=" << step.nugatory_crossing_moves;
                            emit_progress(run_options, message.str());
                        }
                        if (stop_if_quit_at_crossing("non_monotone")) {
                            break;
                        }
                    }
                    restart_round = true;
                    return true;
                }
                record_adaptive_stage_miss(adaptive_scheduler, AdaptiveStage::NonMonotone);
                return false;
            };

            if (adaptive_mode) {
                if (big_heuristic_mode) {
                    const bool use_heuristic_soft_slice = false;
                    if (!run_heuristic_search(use_heuristic_soft_slice)) {
                        output.code = canonical_output_code(output.code);
                        emit_progress(run_options, "non_heuristic_handoff canonicalized=yes");
                        const std::vector<AdaptiveStage> order =
                            adaptive_stage_order(adaptive_scheduler);
                        emit_adaptive_scheduler_state(run_options, round, adaptive_scheduler, order);
                        for (AdaptiveStage stage : order) {
                            if (stage == AdaptiveStage::HeuristicSearch) {
                                continue;
                            }
                            if (stage == AdaptiveStage::R3Prepass) {
                                if (run_r3_prepass()) {
                                    break;
                                }
                            } else {
                                if (run_non_monotone()) {
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    const std::vector<AdaptiveStage> order =
                        adaptive_stage_order(adaptive_scheduler);
                    emit_adaptive_scheduler_state(run_options, round, adaptive_scheduler, order);
                    for (AdaptiveStage stage : order) {
                        if (stage == AdaptiveStage::R3Prepass) {
                            if (run_r3_prepass()) {
                                break;
                            }
                        } else if (stage == AdaptiveStage::HeuristicSearch) {
                            const bool use_heuristic_soft_slice = false;
                            if (run_heuristic_search(use_heuristic_soft_slice)) {
                                break;
                            }
                            if (efficient_phase_timed_out) {
                                switch_to_big_heuristic("round " + std::to_string(round));
                                restart_round = true;
                                break;
                            }
                        } else {
                            if (run_non_monotone()) {
                                break;
                            }
                        }
                    }
                }
                if (output.stopped_by_crossing_limit) {
                    break;
                }
                if (restart_round) {
                    continue;
                }
            } else {
                (void)run_heuristic_search(false);
            }

            if (!search.found && adaptive_mode) {
                SimplifierOptions brute_options = run_options;
                brute_options.max_paths = -1;
                brute_options.ban_heuristic = true;
                brute_options.require_applicable = true;
                output.last_path_search_mode = search_mode_for_options(brute_options);
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " brute_fallback_start crossings=" << output.code.size()
                            << " max_thread=" << brute_options.max_threads
                            << " bruteforce_budget=" << brute_options.bruteforce_budget;
                    emit_progress(run_options, message.str());
                }
                SimplificationResult brute = find_simplification(output.code, brute_options);
                output.tested_red_paths += brute.tested_red_paths;
                output.tested_green_paths += brute.tested_green_paths;
                output.last_path_search_mode = brute.path_search_mode;
                output.resource_limited = output.resource_limited || brute.resource_limited;
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " brute_fallback_done found=" << (brute.found ? "yes" : "no")
                            << " tested_red=" << brute.tested_red_paths
                            << " tested_green=" << brute.tested_green_paths
                            << " resource_limited=" << (brute.resource_limited ? "yes" : "no");
                    emit_progress(run_options, message.str());
                }
                if (brute.found) {
                    ++output.heuristic_failover_rounds;
                    search = std::move(brute);
                }
            }

            if (!search.found) {
                if (output.resource_limited) {
                    std::ostringstream message;
                    message << "round " << round
                            << " stop_resource_limited crossings=" << output.code.size()
                            << " tested_red_total=" << output.tested_red_paths
                            << " tested_green_total=" << output.tested_green_paths;
                    emit_progress(run_options, message.str());
                    break;
                }
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " r3_failover_start crossings=" << output.code.size()
                            << " max_depth=" << kR3FailoverMaxDepth
                            << " max_states=" << kR3FailoverMaxStates;
                    emit_progress(run_options, message.str());
                }
                ReidemeisterIIIFailoverResult r3;
                try {
                    const SimplifierOptions r3_options =
                        with_time_slice(run_options, kR3FailoverTimeSliceSeconds);
                    r3 = find_reidemeister_iii_failover(
                        output.code,
                        output.crossingless_components,
                        r3_options);
                } catch (const TimeoutError&) {
                    if (timeout_expired(run_options)) {
                        throw;
                    }
                    std::ostringstream timeout_message;
                    timeout_message << "round " << round
                                    << " r3_failover_timeout seconds="
                                    << kR3FailoverTimeSliceSeconds;
                    emit_progress(run_options, timeout_message.str());
                }
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " r3_failover_done found=" << (r3.found ? "yes" : "no")
                            << " depth=" << r3.depth
                            << " visited_states=" << r3.visited_states
                            << " final_crossings=" << (r3.found ? r3.code.size() : output.code.size())
                            << " r1_moves=" << r3.reidemeister_i_moves
                            << " r2_moves=" << r3.reidemeister_ii_moves
                            << " r3_moves=" << r3.reidemeister_iii_moves
                            << " nugatory_moves=" << r3.nugatory_crossing_moves;
                    emit_progress(run_options, message.str());
                }
                if (r3.found) {
                    output.code = canonical_output_code(r3.code);
                    output.crossingless_components = r3.crossingless_components;
                    seen_reduction_states.insert(format_final_pd_code(output.code));
                    if (output.code.size() < best_code.size()) {
                        best_code = output.code;
                        best_crossingless_components = output.crossingless_components;
                    }
                    output.reidemeister_i_moves += r3.reidemeister_i_moves;
                    output.reidemeister_ii_moves += r3.reidemeister_ii_moves;
                    output.reidemeister_iii_moves += r3.reidemeister_iii_moves;
                    output.nugatory_crossing_moves += r3.nugatory_crossing_moves;
                    if (stop_if_quit_at_crossing("r3_failover")) {
                        break;
                    }
                    continue;
                }
                {
                    std::ostringstream message;
                    message << "round " << round
                            << " stop_no_path crossings=" << output.code.size();
                    emit_progress(run_options, message.str());
                }
                break;
            }

            const std::size_t before_apply_crossings = output.code.size();
            check_timeout(run_options);
            const MidSimplificationApplyResult applied =
                apply_simplification_witness(output.code, search, output.crossingless_components);
            const bool heuristic_witness = search.path_search_mode == "heuristic";
            const bool preserve_heuristic_order = heuristic_witness && big_heuristic_mode;
            const PDCode applied_code =
                preserve_heuristic_order ? applied.code : canonical_output_code(applied.code);
            ++output.mid_simplification_rounds;
            emit_step_pd(run_options, round, canonical_output_code(applied.code));
            output.code = applied_code;
            output.crossingless_components = applied.crossingless_components;
            check_timeout(run_options);
            const PDSimplificationResult simplified =
                simplify_pd_code_checked(
                    output.code,
                    output.crossingless_components,
                    &run_options,
                    !preserve_heuristic_order,
                    !preserve_heuristic_order);
            output.code = preserve_heuristic_order
                ? simplified.code
                : canonical_output_code(simplified.code);
            output.crossingless_components = simplified.crossingless_components;
            seen_reduction_states.insert(format_final_pd_code(output.code));
            if (output.code.size() < best_code.size()) {
                best_code = output.code;
                best_crossingless_components = output.crossingless_components;
            }
            output.reidemeister_i_moves += simplified.reidemeister_i_moves;
            output.reidemeister_ii_moves += simplified.reidemeister_ii_moves;
            output.nugatory_crossing_moves += simplified.nugatory_crossing_moves;
            check_timeout(run_options);
            {
                std::ostringstream message;
                message << "round " << round
                        << " applied crossings=" << before_apply_crossings
                        << " -> " << applied.code.size()
                        << " -> " << output.code.size()
                        << " crossingless_components=" << output.crossingless_components
                        << " r1_moves=" << simplified.reidemeister_i_moves
                        << " r2_moves=" << simplified.reidemeister_ii_moves
                        << " nugatory_moves=" << simplified.nugatory_crossing_moves;
                emit_progress(run_options, message.str());
            }
            (void)stop_if_quit_at_crossing("mid_simplification");
        }
    } catch (const TimeoutError& error) {
        output.timed_out = true;
        if (best_code.size() < output.code.size()) {
            output.code = best_code;
            output.crossingless_components = best_crossingless_components;
        }
        {
            std::ostringstream message;
            message << error.what()
                    << "; returning_current_best crossings=" << output.code.size()
                    << " crossingless_components=" << output.crossingless_components
                    << " mid_rounds=" << output.mid_simplification_rounds;
            emit_progress(run_options, message.str());
        }
    }

    output.stopped_by_round_limit =
        !output.timed_out &&
        !output.resource_limited &&
        !output.stopped_by_crossing_limit &&
        reduction_round >= 0 &&
        output.mid_simplification_rounds >= reduction_round;
    output.code = canonical_output_code(output.code);
    {
        std::ostringstream message;
        message << "done final_crossings=" << output.code.size()
                << " crossingless_components=" << output.crossingless_components
                << " mid_rounds=" << output.mid_simplification_rounds
                << " heuristic_failover_rounds=" << output.heuristic_failover_rounds
                << " reapr_used=" << (output.reapr_used ? "yes" : "no")
                << " reapr_status=" << output.reapr_status
                << " r2_moves=" << output.reidemeister_ii_moves
                << " r3_moves=" << output.reidemeister_iii_moves
                << " stopped_by_round_limit="
                << (output.stopped_by_round_limit ? "yes" : "no")
                << " stopped_by_crossing_limit="
                << (output.stopped_by_crossing_limit ? "yes" : "no")
                << " timed_out=" << (output.timed_out ? "yes" : "no")
                << " resource_limited=" << (output.resource_limited ? "yes" : "no");
        emit_progress(run_options, message.str());
    }
    return output;
}

inline std::ostream& operator<<(std::ostream& out, const Endpoint& endpoint) {
    out << format_endpoint(endpoint);
    return out;
}

#endif  // PDCODE_SIMPLIFY_DECLARATIONS_ONLY

}  // namespace pdcode_simplify
