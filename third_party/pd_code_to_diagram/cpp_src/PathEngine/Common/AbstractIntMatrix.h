#pragma once

#include <vector>
#include <iomanip>
#include <iostream>

class AbstractIntMatrix {
public:
    virtual ~AbstractIntMatrix() {}

    virtual int getPos(int i, int j) const = 0;
    virtual void setPos(int i, int j, int v) = 0;

    virtual int getRowCnt() const = 0;
    virtual int getColCnt() const = 0;
    
    virtual void debugOutput(std::ostream& out, bool with_zero) const = 0;
};
