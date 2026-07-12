#pragma once

#include <algorithm>
#include <iostream>
#include <set>
#include <tuple>
#include <vector>
#include "../../Utils/MyAssert.h"

template<typename _T>
std::set<_T> mergeSet(const std::set<_T>& s1, const std::set<_T>& s2) {
    std::set<_T> ans;
    for(const auto& item: s1) {
        ans.insert(item);
    }
    for(const auto& item: s2) {
        ans.insert(item);
    }
    return ans;
}

template<typename _T>
std::vector<_T> setToVec(const std::set<_T>& s) {
    std::vector<_T> ans;
    for(const auto& item: s) {
        ans.push_back(item);
    }
    return ans;
}

class Coord2dSet {
private:
    std::set<int> xIntSet;
    std::set<int> yIntSet;

    std::vector<int> xIntVec;
    std::vector<int> yIntVec;

public:
    void addPos(int x, int y) {
        xIntSet.insert(x);
        yIntSet.insert(y);
    }

    static Coord2dSet merge(Coord2dSet cs1, Coord2dSet cs2) {
        Coord2dSet ans;
        ans.xIntSet = mergeSet(cs1.xIntSet, cs2.xIntSet);
        ans.yIntSet = mergeSet(cs1.yIntSet, cs2.yIntSet);
        return ans;
    }

    void setToVec() {
        if(xIntVec.size() != xIntSet.size() || yIntVec.size() != yIntSet.size()) {
            xIntVec = ::setToVec(xIntSet);
            yIntVec = ::setToVec(yIntSet);
        }
    }

    int xRank(int x) {
        setToVec();
        ASSERT(xIntSet.find(x) != xIntSet.end());
        return std::lower_bound(xIntVec.begin(), xIntVec.end(), x) - xIntVec.begin();
    }

    int yRank(int y) {
        setToVec();
        ASSERT(yIntSet.find(y) != yIntSet.end());
        return std::lower_bound(yIntVec.begin(), yIntVec.end(), y) - yIntVec.begin();
    }

    int xkRank(int x, int k) {
        ASSERT(k >= 1);
        setToVec();
        ASSERT(xIntSet.find(x) != xIntSet.end());
        return xRank(x) * k;
    }

    int ykRank(int y, int k) {
        ASSERT(k >= 1);
        setToVec();
        ASSERT(yIntSet.find(y) != yIntSet.end());
        return yRank(y) * k;
    }
};
