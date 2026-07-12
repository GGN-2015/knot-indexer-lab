#pragma once

#ifndef DEBUG_INT_MATRIX
    #define DEBUG_INT_MATRIX (0)
#else
    #undef DEBUG_INT_MATRIX
    #define DEBUG_INT_MATRIX (1)
#endif

#include <iomanip>
#include <iostream>
#include <vector>

#include "AbstractIntMatrix2.h"
#include "../IntMap/AbstractIntMap.h"
#include "../IntCombine/AbstractIntCombine.h"
#include "../../Utils/MyAssert.h"
#include "../../Utils/Debug.h"

class IntMatrix2: public AbstractIntMatrix2 {
protected:
    std::vector<std::vector<int>> matrix_data;
    int rcnt, ccnt;

public:
    virtual ~IntMatrix2() {}

    IntMatrix2(int r, int c) {
        rcnt = r;
        ccnt = c;


        std::vector<int> zeros;
        for(int i = 0; i < c; i += 1) {
            zeros.push_back(0);
        }


        for(int i = 0; i < r; i += 1) {
            matrix_data.push_back(zeros);
        }
    }

    virtual int getRcnt() const override {
        return rcnt;
    }
    virtual int getCcnt() const override {
        return ccnt;
    }

    virtual int getPos(int i, int j) const override {

        if(!(0 <= i && i < rcnt && 0 <= j && j < ccnt)) {
            SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_INT_MATRIX, 
                std::string("(") 
                + std::to_string(i) + ", "
                + std::to_string(j) + ") is outside of ("
                + std::to_string(rcnt) + ", " + std::to_string(ccnt) + ")");
        }
        ASSERT(0 <= i && i < rcnt && 0 <= j && j < ccnt);
        return matrix_data[i][j];
    }
    virtual void setPos(int i, int j, int v) {
        ASSERT(0 <= i && i < rcnt && 0 <= j && j < ccnt);
        matrix_data[i][j] = v;
    }


    virtual void mapAll(const AbstractIntMap& int_map_func) {
        for(int i = 0; i < rcnt; i += 1) {
            for(int j = 0; j < ccnt; j += 1) {
                matrix_data[i][j] = int_map_func.mapInt(i, j, matrix_data[i][j]);
            }
        }
    }


    virtual IntMatrix2 combineMatrix(const IntMatrix2& rhs, const AbstractIntCombine& aci) {
        ASSERT(getRcnt() == rhs.getRcnt());
        ASSERT(getCcnt() == rhs.getCcnt());

        auto ans = IntMatrix2(rcnt, ccnt);
        for(int i = 0; i < rcnt; i += 1) {
            for(int j = 0; j < ccnt; j += 1) {
                ans.setPos(i, j, aci.combineInt(i, j, getPos(i, j), rhs.getPos(i, j)));
            }
        }
        return ans;
    }


    virtual int getMax() const {
        int ans = -2147482648;
        for(int i = 0; i < getRcnt(); i += 1) {
            for(int j = 0; j < getCcnt(); j += 1) {
                ans = std::max(ans, getPos(i, j));
            }
        }
        return ans;
    }
};
