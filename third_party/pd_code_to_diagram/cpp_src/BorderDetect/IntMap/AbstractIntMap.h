#pragma once

class AbstractIntMap {
public:
    virtual ~AbstractIntMap() {}
    virtual int mapInt(int xpos, int ypos, int vFrom) const = 0;
};
