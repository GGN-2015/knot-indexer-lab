#pragma once

#include <algorithm>
#include <map>
#include <tuple>

#include "AbstractGraphEngine.h"
#include "../Common/LineData.h"

class PixelGraphEngine: public AbstractGraphEngine {
private:
    std::map<std::tuple<int, int>, int> pixelValue;

public:
    virtual ~PixelGraphEngine(){}

    static constexpr int INT_INF = 0x7fffffff;

    virtual int getPos(int x, int y) const override {
        auto posNow = std::make_tuple(x, y);
        if(pixelValue.find(posNow) != pixelValue.end()) {
            return pixelValue.find(posNow) -> second;
        }
        return 0;
    };




    virtual std::tuple<int, int, int, int> getBorderCoord() const override {

        int xmin = +INT_INF;
        int xmax = -INT_INF;
        int ymin = +INT_INF;
        int ymax = -INT_INF;

        for(const auto& pr: pixelValue) { 
            int xnow = std::get<0>(pr.first);
            int ynow = std::get<1>(pr.first);
            int vnow = pr.second;

            if(vnow != 0) {
                xmin = std::min(xnow, xmin);
                xmax = std::max(xnow, xmax);
                ymin = std::min(ynow, ymin);
                ymax = std::max(ynow, ymax);
            }
        }
        return std::make_tuple(xmin, xmax, ymin, ymax);
    }


    virtual void setPos(int x, int y, int v) override {
        if(v != 0) {
            pixelValue[std::make_tuple(x, y)] = v;
        }else {


            if(pixelValue.find(std::make_tuple(x, y)) != pixelValue.end()) {
                pixelValue.erase(std::make_tuple(x, y));
            }
        }
    }

    virtual std::vector<std::tuple<int, int>> getAllNegPos() const override {
        std::vector<std::tuple<int, int>> pos_list;
        for(auto pr: pixelValue) {
            if(pr.second < 0) {
                pos_list.push_back(pr.first);
            }
        }
        return pos_list;
    }
};
