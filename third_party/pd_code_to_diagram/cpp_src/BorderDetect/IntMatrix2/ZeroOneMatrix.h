#pragma once

#include <set>

#include "IntMatrix2.h"
#include "../IntMap/NotIntMap.h"
#include "../../Utils/MyAssert.h"

class ZeroOneMatrix: public IntMatrix2 {
public:
    virtual ~ZeroOneMatrix() {}
    ZeroOneMatrix(int rcnt, int ccnt): IntMatrix2(rcnt, ccnt) {}
    ZeroOneMatrix(const AbstractIntMatrix2& mat): IntMatrix2(mat.getRcnt(), mat.getCcnt()) {
        for(int i = 0; i < rcnt; i += 1) {
            for(int j = 0; j < ccnt; j += 1) {
                setPos(i, j, mat.getPos(i, j) != 0);
            }
        }
    }

    virtual void setPos(int i, int j, int v) override {
        ASSERT(v == 0 || v == 1);
        this -> IntMatrix2::setPos(i, j, v);
    }

    virtual int getPos(int i, int j) const override {
        return this -> IntMatrix2::getPos(i, j) != 0;
    }


    virtual void notAll() {
        mapAll(NotIntMap());
    }


    virtual std::set<int> select(const AbstractIntMatrix2& mat) const {
        std::set<int> ans;
        for(int i = 0; i < getRcnt(); i += 1) {
            for(int j = 0; j < getCcnt(); j += 1) {
                if(getPos(i, j)) {
                    ans.insert(mat.getPos(i, j));
                }
            }
        }
        return ans;
    }
};
