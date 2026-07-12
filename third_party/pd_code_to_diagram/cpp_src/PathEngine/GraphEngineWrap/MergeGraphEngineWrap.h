#pragma once

#include <iostream>
#include <set>
#include "../GraphEngine/AbstractGraphEngine.h"
#include "../../Utils/MyAssert.h"



class MergeGraphEngineWrap: public AbstractGraphEngine {
private:
    const AbstractGraphEngine& age_front;
    const AbstractGraphEngine& age_next;

public:
    virtual ~MergeGraphEngineWrap(){}

    MergeGraphEngineWrap(
        const AbstractGraphEngine& _age_front, const AbstractGraphEngine& _age_next):
        age_front(_age_front), age_next(_age_next){}
    

    virtual int getPos(int x, int y) const override {
        auto front_val = age_front.getPos(x, y);
        return front_val == 0 ? age_next.getPos(x, y) : front_val;
    }


    virtual std::tuple<int, int, int, int> getBorderCoord() const override {


        auto tuple_4int = age_front.getBorderCoord();
        int xmin_front = std::get<0>(tuple_4int);
        int xmax_front = std::get<1>(tuple_4int);
        int ymin_front = std::get<2>(tuple_4int);
        int ymax_front = std::get<3>(tuple_4int);
        

        auto tuple_4int2 = age_next.getBorderCoord();
        int xmin_next = std::get<0>(tuple_4int2);
        int xmax_next = std::get<1>(tuple_4int2);
        int ymin_next = std::get<2>(tuple_4int2);
        int ymax_next = std::get<3>(tuple_4int2);

        return std::make_tuple(
            std::min(xmin_front, xmin_next),
            std::max(xmax_front, xmax_next),
            std::min(ymin_front, ymin_next),
            std::max(ymax_front, ymax_next));
    }

    virtual std::vector<std::tuple<int, int>> getAllNegPos() const override {
        auto pos_list1 = age_front.getAllNegPos();
        auto pos_list2 = age_next.getAllNegPos();

        std::set<std::tuple<int, int>> new_pos_set;
        for(auto item: pos_list1) {
            if(getPos(std::get<0>(item), std::get<1>(item)) < 0) {
                new_pos_set.insert(item);
            }
        }
        for(auto item: pos_list2) {
            if(getPos(std::get<0>(item), std::get<1>(item)) < 0) {
                new_pos_set.insert(item);
            }
        }
        std::vector<std::tuple<int, int>> new_pos_list;
        for(auto item: new_pos_set) {
            new_pos_list.push_back(item);
        }
        return new_pos_list;
    }


    virtual void setPos(int x, int y, int v) override {
        std::cerr << "error: can not setPos for MergeGraphEngineWrap" << std::endl;
        ASSERT(false);
    }
};
