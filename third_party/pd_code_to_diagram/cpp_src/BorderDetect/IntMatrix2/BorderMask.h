#pragma once

#include <memory>
#include "ZeroOneMatrix.h"

class BorderMask: public AbstractIntMatrix2 {
protected:
    std::shared_ptr<AbstractIntMatrix2> aim;
    int body_val;

public:
    virtual ~BorderMask() {}
    BorderMask(std::shared_ptr<AbstractIntMatrix2> _aim, int _body_val): 
        aim(_aim), body_val(_body_val) {
    }

    virtual int getRcnt() const override {
        return aim -> getRcnt();
    }
    virtual int getCcnt() const override {
        return aim -> getCcnt();
    }

    virtual int getPos(int i, int j) const override {


        if(aim -> getPos(i, j) != body_val) {
            return false;
        }


        const int dx[] = {1, -1,  0, 0};
        const int dy[] = {0,  0, -1, 1};


        int cnt = 0;
        for(int d = 0; d < 4; d += 1) {
            int nx = i + dx[d];
            int ny = j + dy[d];
            if(0 <= nx && nx < getRcnt() && 0 <= ny && ny < getCcnt()) {
                if(aim -> getPos(nx, ny) != body_val) {
                    cnt += 1;
                }
            }
        }


        return cnt > 0;
    }


    virtual int getMax() const {
        return 1;
    }
};
