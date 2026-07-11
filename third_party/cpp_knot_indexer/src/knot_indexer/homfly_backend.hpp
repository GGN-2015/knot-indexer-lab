#pragma once

#include "pd_code.hpp"

#include <string>

namespace hki {

std::string computeHomflyPT(const PDCode& pd);

} // namespace hki

#include "khovanov_backend.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

#ifndef HKI_LIBHOMFLY_MINIMAL_DECLS
#define HKI_LIBHOMFLY_MINIMAL_DECLS
typedef signed long int hki_homfly_sb4;
typedef signed short int hki_homfly_sb2;

struct Term {
    hki_homfly_sb4 coef;
    hki_homfly_sb2 m;
    hki_homfly_sb2 l;
};

struct Poly {
    Term* term;
    hki_homfly_sb4 len;
};

Poly* homfly(char* argv);
#endif

namespace hki {

namespace {

struct HalfEdge {
    int crossing = -1;
    int position = -1;
};

struct PassStep {
    int crossing = -1;
    int strand = -1;
    int fromPosition = -1;
    int toPosition = -1;
};

struct ComponentPath {
    std::vector<PassStep> steps;
    std::vector<int> labels;
};

struct TermOut {
    long coef = 0;
    int m = 0;
    int l = 0;
};

int strandOfPosition(int position) {
    return position % 2;
}

int oppositePosition(int position) {
    return position ^ 2;
}

int passId(const HalfEdge& half) {
    return half.crossing * 2 + strandOfPosition(half.position);
}

std::map<int, std::array<HalfEdge, 2>> buildLabelMap(const PDCode& pd) {
    std::map<int, std::vector<HalfEdge>> raw;
    for (size_t i = 0; i < pd.size(); ++i) {
        for (int position = 0; position < 4; ++position) {
            raw[pd[i][position]].push_back(HalfEdge{static_cast<int>(i), position});
        }
    }

    std::map<int, std::array<HalfEdge, 2>> labels;
    for (const auto& item : raw) {
        if (item.second.size() != 2) {
            throw std::runtime_error("PD labels must appear exactly twice");
        }
        labels[item.first] = {item.second[0], item.second[1]};
    }
    return labels;
}

int labelAt(const PDCode& pd, const HalfEdge& half) {
    return pd[static_cast<size_t>(half.crossing)][static_cast<size_t>(half.position)];
}

HalfEdge otherOccurrence(const std::map<int, std::array<HalfEdge, 2>>& labels,
                         int label,
                         const HalfEdge& half) {
    auto found = labels.find(label);
    if (found == labels.end()) throw std::runtime_error("internal PD label lookup failed");
    const auto& pair = found->second;
    if (pair[0].crossing == half.crossing && pair[0].position == half.position) return pair[1];
    if (pair[1].crossing == half.crossing && pair[1].position == half.position) return pair[0];
    throw std::runtime_error("internal PD half-edge lookup failed");
}

ComponentPath traceComponentFrom(const PDCode& pd,
                                 const std::map<int, std::array<HalfEdge, 2>>& labels,
                                 HalfEdge start) {
    ComponentPath path;
    HalfEdge current = start;
    std::set<std::pair<int, int>> seenHalfEdges;

    for (;;) {
        if (!seenHalfEdges.insert({current.crossing, current.position}).second) {
            if (current.crossing == start.crossing && current.position == start.position) break;
            throw std::runtime_error("PD component traversal found a non-cycle");
        }

        const int fromLabel = labelAt(pd, current);
        const int toPosition = oppositePosition(current.position);
        const int toLabel = pd[static_cast<size_t>(current.crossing)][static_cast<size_t>(toPosition)];
        path.labels.push_back(fromLabel);
        path.steps.push_back(PassStep{
            current.crossing,
            strandOfPosition(current.position),
            current.position,
            toPosition,
        });

        HalfEdge exit{current.crossing, toPosition};
        current = otherOccurrence(labels, toLabel, exit);
    }

    return path;
}

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> pathKey(const ComponentPath& path) {
    std::vector<int> crossings;
    std::vector<int> strands;
    crossings.reserve(path.steps.size());
    strands.reserve(path.steps.size());
    for (const PassStep& step : path.steps) {
        crossings.push_back(step.crossing);
        strands.push_back(step.strand);
    }
    return {path.labels, crossings, strands};
}

ComponentPath chooseComponentOrientation(const PDCode& pd,
                                         const std::map<int, std::array<HalfEdge, 2>>& labels,
                                         int minLabel) {
    auto found = labels.find(minLabel);
    if (found == labels.end()) throw std::runtime_error("internal component start lookup failed");

    ComponentPath first = traceComponentFrom(pd, labels, found->second[0]);
    ComponentPath second = traceComponentFrom(pd, labels, found->second[1]);
    return pathKey(second) < pathKey(first) ? second : first;
}

int crossingHandFromTraversal(const std::array<PassStep, 2>& crossingSteps) {
    const PassStep& under = crossingSteps[0];
    const PassStep& over = crossingSteps[1];
    if (under.crossing < 0 || over.crossing < 0) {
        throw std::runtime_error("PD traversal did not visit both strands of a crossing");
    }
    const bool underForward = under.fromPosition == 0 && under.toPosition == 2;
    const bool underBackward = under.fromPosition == 2 && under.toPosition == 0;
    const bool overBackward = over.fromPosition == 3 && over.toPosition == 1;
    const bool overForward = over.fromPosition == 1 && over.toPosition == 3;
    if ((!underForward && !underBackward) || (!overForward && !overBackward)) {
        throw std::runtime_error("PD crossing has invalid opposite-pair traversal");
    }
    return underForward == overBackward ? 1 : -1;
}

std::string toJenkinsInput(const PDCode& pd) {
    const auto labels = buildLabelMap(pd);
    std::set<int> unvisitedPasses;
    for (size_t i = 0; i < pd.size(); ++i) {
        unvisitedPasses.insert(static_cast<int>(i) * 2);
        unvisitedPasses.insert(static_cast<int>(i) * 2 + 1);
    }

    std::vector<ComponentPath> components;
    std::vector<std::array<PassStep, 2>> crossingSteps(pd.size());
    while (!unvisitedPasses.empty()) {
        int minLabel = 0;
        bool haveLabel = false;
        for (const auto& item : labels) {
            const HalfEdge& a = item.second[0];
            const HalfEdge& b = item.second[1];
            if (unvisitedPasses.count(passId(a)) || unvisitedPasses.count(passId(b))) {
                minLabel = item.first;
                haveLabel = true;
                break;
            }
        }
        if (!haveLabel) break;

        ComponentPath component = chooseComponentOrientation(pd, labels, minLabel);
        for (const PassStep& step : component.steps) {
            const int id = step.crossing * 2 + step.strand;
            auto erased = unvisitedPasses.erase(id);
            if (!erased) throw std::runtime_error("PD component traversal reused a strand pass");
            crossingSteps[static_cast<size_t>(step.crossing)][static_cast<size_t>(step.strand)] = step;
        }
        components.push_back(std::move(component));
    }

    std::ostringstream out;
    out << components.size() << " ";

    for (const ComponentPath& component : components) {
        out << component.steps.size() << " ";
        for (const PassStep& step : component.steps) {
            out << step.crossing << " " << (step.strand == 1 ? 1 : -1) << " ";
        }
    }

    for (size_t i = 0; i < pd.size(); ++i) {
        out << i << " " << crossingHandFromTraversal(crossingSteps[i]) << " ";
    }
    return out.str();
}

std::string formatOneVariable(const char* name, int exp) {
    if (exp == 0) return "";
    std::ostringstream out;
    out << name;
    if (exp != 1) out << "^" << exp;
    return out.str();
}

std::string formatPolynomial(const Poly* poly, bool mirrorL) {
    if (!poly || poly->len == 0) return "0";

    std::vector<TermOut> terms;
    terms.reserve(static_cast<size_t>(poly->len));
    for (long i = 0; i < poly->len; ++i) {
        long coef = poly->term[i].coef;
        if (coef == 0) continue;
        int m = poly->term[i].m;
        int l = poly->term[i].l;
        if (mirrorL) l = -l;
        terms.push_back(TermOut{coef, m, l});
    }

    std::sort(terms.begin(), terms.end(), [](const TermOut& a, const TermOut& b) {
        int totalA = a.l + a.m;
        int totalB = b.l + b.m;
        if (totalA != totalB) return totalA > totalB;
        if (a.l != b.l) return a.l > b.l;
        return a.m > b.m;
    });

    std::ostringstream out;
    bool first = true;
    for (const TermOut& term : terms) {
        long absCoef = term.coef < 0 ? -term.coef : term.coef;
        bool hasVars = term.l != 0 || term.m != 0;

        if (first) {
            if (term.coef < 0) out << "-";
            first = false;
        } else {
            out << (term.coef < 0 ? " - " : " + ");
        }

        if (!hasVars || absCoef != 1) {
            out << absCoef;
            if (hasVars) out << "*";
        }

        if (hasVars) {
            std::string lPart = formatOneVariable("L", term.l);
            std::string mPart = formatOneVariable("M", term.m);
            if (!lPart.empty()) out << lPart;
            if (!lPart.empty() && !mPart.empty()) out << "*";
            if (!mPart.empty()) out << mPart;
        }
    }

    std::string value = out.str();
    return value.empty() ? "0" : value;
}

} // namespace

inline std::string computeHomflyPT(const PDCode& inputPd) {
    PDCode pd = simplifyPDCode(inputPd);
    if (pd.empty()) return "1";

    std::string input = toJenkinsInput(pd);
    Poly* poly = homfly(&input[0]);
    if (!poly) throw std::runtime_error("libhomfly failed to compute the polynomial");

    return formatPolynomial(poly, true);
}

} // namespace hki
