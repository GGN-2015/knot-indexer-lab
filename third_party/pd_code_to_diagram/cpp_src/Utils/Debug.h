#pragma once

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <string>

template<typename _K, typename _V>
void debugMap(std::string map_name, const std::map<_K, _V>& kv_map) {
    std::cout << map_name << ": " << std::endl;
    for(const auto& k_v: kv_map) {
        std::cout << k_v.first << ": " << k_v.second << std::endl;
    }
}


#define SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_FLAG, MSG) { \
    if(DEBUG_FLAG) { \
        std::cerr << MSG << std::endl; \
    } \
}


#define SHOW_DEBUG_MESSAGE(MSG) { \
    SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG, MSG) \
}


bool isAllDigits(const std::string& str) {
    if (str.empty()) {
        return false;
    }



    return std::all_of(str.begin(), str.end(), 
        [](unsigned char c) { return std::isdigit(c); });
}
