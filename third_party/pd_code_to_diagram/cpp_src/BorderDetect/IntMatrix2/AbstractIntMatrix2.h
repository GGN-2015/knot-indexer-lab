#pragma once

#include <iomanip>
#include <iostream>
#include <vector>

#include "../IntMap/AbstractIntMap.h"
#include "../IntCombine/AbstractIntCombine.h"

class AbstractIntMatrix2 {
public:
    virtual ~AbstractIntMatrix2() {}

    virtual int getRcnt() const = 0;
    virtual int getCcnt() const = 0;

    virtual int getPos(int i, int j) const = 0;


    virtual void debugOutput(std::ostream& out, int width) const {
        for(int i = 0; i < getRcnt(); i += 1) {
            for(int j = 0; j < getCcnt(); j += 1) {
                out << std::setw(width) << getPos(i, j) << " ";
            }
            out << std::endl;
        }
    }


    virtual int getMax() const = 0;
};
