#pragma once

#include "pd_code.hpp"

#include <string>

namespace hki {

std::string computeSimplifiedPDCode(const PDCode& pd);

}  // namespace hki

#include "runtime_control.hpp"

#include "pdcode_simplify/pdcode_simplify.hpp"

namespace hki {

namespace {

pdcode_simplify::PDCode toSimplifierPDCode(const PDCode& pd) {
    pdcode_simplify::PDCode converted;
    converted.reserve(pd.size());
    for (const Crossing& crossing : pd) {
        converted.push_back(pdcode_simplify::Crossing{
            crossing[0],
            crossing[1],
            crossing[2],
            crossing[3],
        });
    }
    return converted;
}

}  // namespace

inline std::string computeSimplifiedPDCode(const PDCode& pd) {
    pdcode_simplify::SimplifierOptions options;
    options.max_threads = -1;
    options.timeout_seconds = -1;
    options.should_cancel = []() {
        return interrupted();
    };

    const pdcode_simplify::ReductionResult result =
        pdcode_simplify::reduce_pd_code(toSimplifierPDCode(pd), 0, options, -1);
    return formatPDCodeList(parsePDCode(pdcode_simplify::format_final_pd_code(result.code)));
}

}  // namespace hki
