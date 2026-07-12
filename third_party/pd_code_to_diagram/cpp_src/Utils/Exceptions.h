#pragma once

#include <stdexcept>


#define THROW_EXCEPTION(EXCEPTION_TYPE, MSG) \
    throw EXCEPTION_TYPE(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " + std::string(#EXCEPTION_TYPE) + ":" + std::string(MSG))


#define DEFINE_EXCEPTION(EXCEPTION_TYPE) \
class EXCEPTION_TYPE: public std::exception { \
private: \
    std::string error_msg; \
public: \
    explicit EXCEPTION_TYPE(std::string msg) : error_msg(std::move(msg)) {} \
    const char* what() const noexcept override { \
        return error_msg.c_str(); \
    } \
}




#define PROCESS_EXCEPTION(EXCEPTION_TYPE, OTHER_CMD) \
catch(const EXCEPTION_TYPE& re){ \
    SHOW_DEBUG_MESSAGE(re.what()); \
    OTHER_CMD; \
}



DEFINE_EXCEPTION(BadBorderException);



DEFINE_EXCEPTION(CrossingMeetException);


DEFINE_EXCEPTION(MaxTryExceeded);
