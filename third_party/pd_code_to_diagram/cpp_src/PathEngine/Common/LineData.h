#pragma once

#include "../../Utils/MyAssert.h"

class LineData
{
private:
    int xf, xt, yf, yt, v;

public:
    LineData(int _xf, int _xt, int _yf, int _yt, int _v) {
        ASSERT(_xf == _xt || _yf == _yt);
        xf = _xf;
        xt = _xt;
        yf = _yf;
        yt = _yt;
        v  = _v;
    }
    int getXf() const {return xf;}
    int getXt() const {return xt;}
    int getYf() const {return yf;}
    int getYt() const {return yt;}
    int getV () const {return v ;}

    LineData setV(int new_v) const {
        return LineData(xf, xt, yf, yt, new_v);
    }

    LineData setAimPos(int new_xt, int new_yt) const {
        return LineData(xf, new_xt, yf, new_yt, v);
    }

    LineData setStartPos(int new_xf, int new_yf) const {
        return LineData(new_xf, xt, new_yf, yt, v);
    }
};
