#pragma once

#include <stdexcept>
#include <string>
#include <sstream>



#define ASSERT_THROW(condition, msg)                                          \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::ostringstream oss;                                           \
            oss << "Assertion failed: [" #condition "] "                      \
                << "File: " << __FILE__ << " Line: " << __LINE__ << " "       \
                << "Message: " << msg;                                        \
            throw std::runtime_error(oss.str());                              \
        }                                                                     \
    } while (false)


#define ASSERT(condition)                                                     \
    ASSERT_THROW(condition, "Assertion failed for condition: " #condition)
