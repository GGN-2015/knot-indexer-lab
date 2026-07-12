#pragma once

#include <iostream>
#include "../IntMatrix2/IntMatrix2.h"

class AbstractDataInput {
public:
    virtual ~AbstractDataInput(){}
    virtual IntMatrix2 loadMatrix(std::istream& in) const = 0;
};
