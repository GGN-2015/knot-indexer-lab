#pragma once

class AbstractIntCombine {
public:
    virtual ~AbstractIntCombine() {}
    virtual int combineInt(int xpos, int ypos, int lhs_val, int rhs_val) const = 0;
};
