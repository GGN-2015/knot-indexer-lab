#pragma once

#include <iostream>

#include "../GraphEngine/AbstractGraphEngine.h"
#include "../../Utils/MyAssert.h"




class MarginGraphEngineWrap: public AbstractGraphEngine{
private:
    int xmin, xmax, ymin, ymax;
    int xf, yf, xt, yt;
    int rawVal;
    const AbstractGraphEngine& age;

public:
    virtual ~MarginGraphEngineWrap(){}
    MarginGraphEngineWrap(const AbstractGraphEngine& _age, 
        int _xmin, int _xmax, int _ymin, int _ymax, int _rawVal,
        int _xf, int _yf, int _xt, int _yt
    ): age(_age) {

        ASSERT(_xmin <= _xmax && _ymin <= _ymax);
        ASSERT(_rawVal != 0);
        xmin = _xmin;
        xmax = _xmax;
        ymin = _ymin;
        ymax = _ymax;
        rawVal = _rawVal;


        xf = _xf;
        yf = _yf;
        xt = _xt;
        yt = _yt;
    }

    virtual std::vector<std::tuple<int, int>> getAllNegPos() const override {
        ASSERT(rawVal >= 0);
        auto pos_list = age.getAllNegPos();
        std::vector<std::tuple<int, int>> new_pos_list;
        for(auto item: pos_list) {
            int x, y;
            std::tie(x, y) = item;
            if(xmin <= x && x <= xmax && ymin <= y && y <= ymax) {
                new_pos_list.push_back(item);
            }
        }
        return new_pos_list;
    }



    virtual int getPos(int x, int y) const override {
        if(x == xf && y == yf) {
            return 0;
        }else
        if(x == xt && y == yt) {
            return 0;
        }else
        if(x < xmin || x > xmax || y < ymin || y > ymax) {
            return rawVal;
        }
        return age.getPos(x, y);
    }

    virtual void setPos(int x, int y, int v) override {
        std::cerr << "error: can not setPos for GraphEngineWrap" << std::endl;
        ASSERT(false);
    }

    virtual std::tuple<int, int, int, int> getBorderCoord() const override {
        return age.getBorderCoord();
    }
};
