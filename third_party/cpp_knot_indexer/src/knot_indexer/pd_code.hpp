#pragma once

#include <array>
#include <string>
#include <vector>

namespace hki {

using Crossing = std::array<int, 4>;
using PDCode = std::vector<Crossing>;

PDCode parsePDCode(const std::string& text);
void validatePDCode(const PDCode& pd);
std::string formatPDCodeList(const PDCode& pd);
std::string formatPDCodeKnotTheory(const PDCode& pd);

} // namespace hki

#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>

namespace hki {

inline PDCode parsePDCode(const std::string& text) {
    std::vector<int> values;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '-' || std::isdigit(static_cast<unsigned char>(text[i]))) {
            int sign = 1;
            if (text[i] == '-') {
                sign = -1;
                ++i;
            }
            if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
                throw std::runtime_error("invalid integer in PD code");
            }
            long long value = 0;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                value = value * 10 + (text[i] - '0');
                if (value > 1000000000LL) throw std::runtime_error("PD arc label is too large");
                ++i;
            }
            values.push_back(static_cast<int>(sign * value));
        } else {
            ++i;
        }
    }

    if (values.empty()) return {};
    if (values.size() % 4 != 0) {
        throw std::runtime_error("PD code must contain groups of four integers");
    }

    PDCode pd;
    pd.reserve(values.size() / 4);
    for (size_t i = 0; i < values.size(); i += 4) {
        pd.push_back(Crossing{values[i], values[i + 1], values[i + 2], values[i + 3]});
    }
    validatePDCode(pd);
    return pd;
}

inline void validatePDCode(const PDCode& pd) {
    std::map<int, int> counts;
    for (const auto& crossing : pd) {
        for (int label : crossing) {
            if (label <= 0) throw std::runtime_error("PD arc labels must be positive integers");
            ++counts[label];
        }
    }
    for (const auto& item : counts) {
        if (item.second != 2) {
            std::ostringstream out;
            out << "PD arc label " << item.first << " appears " << item.second << " times";
            throw std::runtime_error(out.str());
        }
    }
}

inline std::string formatPDCodeList(const PDCode& pd) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < pd.size(); ++i) {
        if (i) out << ",";
        out << "[";
        for (int j = 0; j < 4; ++j) {
            if (j) out << ",";
            out << pd[i][j];
        }
        out << "]";
    }
    out << "]";
    return out.str();
}

inline std::string formatPDCodeKnotTheory(const PDCode& pd) {
    std::ostringstream out;
    out << "PD[";
    for (size_t i = 0; i < pd.size(); ++i) {
        if (i) out << ",";
        out << "X[";
        for (int j = 0; j < 4; ++j) {
            if (j) out << ",";
            out << pd[i][j];
        }
        out << "]";
    }
    out << "]";
    return out.str();
}

} // namespace hki
