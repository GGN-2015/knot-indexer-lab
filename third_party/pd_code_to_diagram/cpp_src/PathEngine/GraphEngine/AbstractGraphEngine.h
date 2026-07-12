#pragma once

#include <iomanip>
#include <tuple>
#include <vector>
#include "../Common/IntMatrix.h"
#include "../Common/LineData.h"
#include "../../Utils/MyAssert.h"


class AbstractGraphEngine {
public:
    virtual ~AbstractGraphEngine(){}



    virtual int getPos(int x, int y) const = 0;


    virtual std::tuple<int, int, int, int> getBorderCoord() const = 0;


    virtual void setPos(int x, int y, int v) = 0;


    virtual std::vector<std::tuple<int, int>> getAllNegPos() const = 0;
    


    virtual void setLine(const LineData& lineData) {
        int xf = lineData.getXf();
        int xt = lineData.getXt();
        int yf = lineData.getYf();
        int yt = lineData.getYt();
        int  v = lineData.getV ();
        ASSERT(xf == xt || yf == yt);
        if(xf == xt) {
            for(int i = std::min(yf, yt); i <= std::max(yf, yt); i += 1) {
                setPos(xf, i, v);
            }
        }else {
            for(int i = std::min(xf, xt); i <= std::max(xf, xt); i += 1) {
                setPos(i, yf, v);
            }
        }
    }


    virtual void debugOutput(std::ostream& out, bool with_zero) const {
        exportToIntMatrix().debugOutput(out, with_zero);
    }

    virtual IntMatrix exportToIntMatrix() const {
        int xmin, xmax, ymin, ymax;
        std::tie(xmin, xmax, ymin, ymax) = getBorderCoord();
        xmin -= 1;
        xmax += 1;
        ymin -= 1;
        ymax += 1;
        
        IntMatrix ans(xmax - xmin + 1, ymax - ymin + 1);
        for(int i = xmin; i <= xmax; i += 1) {
            for(int j = ymin; j <= ymax; j += 1) {
                ans.setPos(i - xmin, j - ymin, getPos(i, j));
            }
        }
        return ans;
    }
};
