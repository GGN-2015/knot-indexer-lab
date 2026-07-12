#pragma once

#include "AbstractIntMap.h"


class NotIntMap: public AbstractIntMap {
public:
    virtual ~NotIntMap() {}
    virtual int mapInt(int xpos, int ypos, int vFrom) const override {
        return vFrom == 0;
    }
};
