#pragma once

#include <vector>
#include "../GraphEngine/AbstractGraphEngine.h"
#include "../Common/LineData.h"

class AbstractPathAlgorithm {
public:
    virtual ~AbstractPathAlgorithm(){}






    virtual 
    std::tuple<double, std::vector<LineData>>
    runAlgo(const AbstractGraphEngine& age,
        int xmin, int xmax, int ymin, int ymax,
        int xf, int yf, int xt, int yt) = 0;
};
