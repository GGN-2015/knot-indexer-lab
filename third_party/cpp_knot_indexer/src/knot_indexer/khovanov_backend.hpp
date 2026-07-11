#pragma once

#include "pd_code.hpp"

#include <string>

namespace hki {

std::string computeKhovanov(const PDCode& pd);
PDCode simplifyPDCode(const PDCode& pd);

} // namespace hki

#include <stdexcept>

extern "C" {
char* cppkh_compute_pd_ex(const char* pd_code, int simplify_pd, int reorder_crossings);
char* cppkh_simplify_pd(const char* pd_code);
const char* cppkh_last_error();
void cppkh_free(char* value);
}

namespace hki {

namespace {

std::string takeCppkhString(char* raw) {
    if (!raw) {
        const char* err = cppkh_last_error();
        throw std::runtime_error(err && *err ? err : "cppkh computation failed");
    }
    std::string value(raw);
    cppkh_free(raw);
    return value;
}

} // namespace

inline std::string computeKhovanov(const PDCode& pd) {
    std::string input = formatPDCodeKnotTheory(pd);
    return takeCppkhString(cppkh_compute_pd_ex(input.c_str(), 1, 1));
}

inline PDCode simplifyPDCode(const PDCode& pd) {
    std::string input = formatPDCodeKnotTheory(pd);
    std::string simplified = takeCppkhString(cppkh_simplify_pd(input.c_str()));
    return parsePDCode(simplified);
}

} // namespace hki
