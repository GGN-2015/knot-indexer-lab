#pragma once

#include <set>
#include <vector>


class AbstractGraph {
public:
    virtual ~AbstractGraph() {}



    virtual int getMaxNodeId() const = 0;


    virtual bool checkHasNode(int nodeId) const = 0;


    virtual std::vector<int> getNextNode(int nodeId) const = 0;
};
