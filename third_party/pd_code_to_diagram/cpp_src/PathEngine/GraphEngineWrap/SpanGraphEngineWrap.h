#pragma once

#include <algorithm>
#include <iostream>
#include <set>
#include <vector>
#include "../GraphEngine/AbstractGraphEngine.h"
#include "../../Utils/MyAssert.h"

class SpanGraphEngineWrap: public AbstractGraphEngine {
private:
    const AbstractGraphEngine& raw_age;

public:
    virtual ~SpanGraphEngineWrap(){}
    SpanGraphEngineWrap(const AbstractGraphEngine& _raw_age): raw_age(_raw_age) {}

    virtual int getPos(int x, int y) const override {
        const int dx[] = {0, 1, 0,-1};
        const int dy[] = {1, 0,-1, 0};


        std::vector<int> non_zero_value_list;
        if(raw_age.getPos(x, y) != 0) {
            non_zero_value_list.push_back(raw_age.getPos(x, y));
        }

        for(int d = 0; d < 4; d += 1) {
            int xpos = x + dx[d];
            int ypos = y + dy[d];
            if(raw_age.getPos(xpos, ypos) != 0) {
                non_zero_value_list.push_back(raw_age.getPos(xpos, ypos));
            }
        }


        if(non_zero_value_list.size() == 0) return 0;
        else {


            return *std::max_element(non_zero_value_list.begin(), non_zero_value_list.end());
        }
    }

    virtual std::tuple<int, int, int, int> getBorderCoord() const override {
        int xmin, xmax, ymin, ymax;
        std::tie(xmin, xmax, ymin, ymax) = raw_age.getBorderCoord();


        return std::make_tuple(xmin - 1, xmax + 1, ymin - 1, ymax + 1);
    }


    virtual void setPos(int x, int y, int v) override {
        std::cerr << "error: can not setPos for SpanGraphEngineWrap" << std::endl;
        ASSERT(false);
    }

    virtual std::vector<std::tuple<int, int>> getAllNegPos() const override {
        static const int dx[] = {0, 1, 0,-1, 0};
        static const int dy[] = {0, 0, 1, 0,-1};
        auto pos_list = raw_age.getAllNegPos();
        std::set<std::tuple<int, int>> new_pos_set;
        for(auto item: pos_list) {
            int x, y;
            std::tie(x, y) = item;
            for(int d = 0; d < 5; d += 1) {
                int xpos = x + dx[d];
                int ypos = y + dy[d];
                if(getPos(xpos, ypos) < 0) {
                    new_pos_set.insert(std::make_tuple(xpos, ypos));
                }
            }
        }
        std::vector<std::tuple<int, int>> new_pos_list;
        for(auto item: new_pos_set) {
            new_pos_list.push_back(item);
        }
        return new_pos_list;
    }
};
