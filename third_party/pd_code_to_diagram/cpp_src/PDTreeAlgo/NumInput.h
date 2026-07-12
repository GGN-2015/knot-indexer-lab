#pragma once

#include <iostream>
#include <vector>
#include <sstream>
#include <cctype>


std::vector<int> extractIntegersFromStream(std::istream& is) {

    std::string processedStr;
    char ch;
    while (is.get(ch)) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            processedStr.push_back(ch);
        } else {
            processedStr.push_back(' ');
        }
    }


    std::vector<int> result;
    std::istringstream iss(processedStr);
    int num;
    while (iss >> num) {
        result.push_back(num);
    }

    return result;
}
