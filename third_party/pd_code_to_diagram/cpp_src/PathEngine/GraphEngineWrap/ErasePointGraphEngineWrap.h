#pragma once

#include <iostream>
#include <set>
#include "../GraphEngine/AbstractGraphEngine.h"
#include "../../Utils/MyAssert.h"



class ErasePointGraphEngineWrap : public AbstractGraphEngine {
private:
    const AbstractGraphEngine& raw_age;
    std::set<std::tuple<int, int>> force_empty_pos;

public:
    virtual ~ErasePointGraphEngineWrap(){}
    ErasePointGraphEngineWrap(const AbstractGraphEngine& _raw_age): raw_age(_raw_age) {}

    void clear() {
        force_empty_pos.clear();
    }
    void addEmptyPos(int x, int y) {
        force_empty_pos.insert(std::make_tuple(x, y));
    }


    virtual std::vector<std::tuple<int, int>> getAllNegPos() const override {
        auto pos_list = raw_age.getAllNegPos();
        std::vector<std::tuple<int, int>> new_pos_list;
        for(auto item: pos_list) {
            if(force_empty_pos.count(item) == 0) {
                new_pos_list.push_back(item);
            }
        }
        return new_pos_list;
    }

    virtual int getPos(int x, int y) const override {
        if(force_empty_pos.size() != 3 && force_empty_pos.size() != 4) {
            std::cerr << "warning: in ErasePointGraphEngineWrap, force_empty_pos.size() != 3 or 4" << std::endl;
            ASSERT(false);
        }


        if(force_empty_pos.count(std::make_tuple(x, y)) > 0) {
            return 0;
        }else {

            return raw_age.getPos(x, y);
        }
    }


    virtual std::tuple<int, int, int, int> getBorderCoord() const override {
        return raw_age.getBorderCoord();
    }


    virtual void setPos(int x, int y, int v) override {
        std::cerr << "error: can not setPos for ErasePointGraphEngineWrap" << std::endl;
        ASSERT(false);
    }
};
