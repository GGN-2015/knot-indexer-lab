#pragma once

#include <iostream>
#include <sstream>
#include <string>


#define REWIND_STRING_STREAM(ss) { \
    ss.clear(); \
    ss.seekg(0, std::ios::beg); \
}


static std::string getStreamContentWithoutChange(std::stringstream& ss) {

    std::ios::pos_type originalPos = ss.tellg();
    

    REWIND_STRING_STREAM(ss);
    

    std::string content((std::istreambuf_iterator<char>(ss)), std::istreambuf_iterator<char>());
    

    ss.seekg(originalPos);
    return content;
}




static std::stringstream readCinToStringStream() {
    std::stringstream ss;
    std::string line;

    try {


        while (std::getline(std::cin, line)) {

            ss << line << '\n';
        }


        if (std::cin.bad()) {
            throw std::runtime_error("fatal error while reading stdin");
        }


        ss.clear();
        ss.seekg(0, std::ios::beg);
    }
    catch (const std::exception& e) {

        throw std::runtime_error("cin error: " + std::string(e.what()));
    }

    return ss;
}
