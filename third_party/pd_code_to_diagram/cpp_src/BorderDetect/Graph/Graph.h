#pragma once

#include "AbstractGraph.h"
#include "../../Utils/MyAssert.h"

class Graph: public AbstractGraph {
protected:
    int maxNodeId;
    std::vector<std::vector<int>> nextNode;

public:
    virtual ~Graph() {}

    Graph(): maxNodeId(0) {
        nextNode.push_back(std::vector<int>());
    }
    Graph(int _maxNodeCnt): maxNodeId(0) {
        nextNode.push_back(std::vector<int>());
        setMaxNodeId(_maxNodeCnt);
    }
    


    virtual int getMaxNodeId() const override {
        return maxNodeId;
    }


    virtual bool checkHasNode(int nodeId) const override {
        return 1 <= nodeId && nodeId <= maxNodeId;
    }


    virtual void setMaxNodeId(int newMaxNodeId) {
        while(maxNodeId < newMaxNodeId) {
            maxNodeId += 1;
            nextNode.push_back({});
        }
    }


    virtual void addEdge(int f, int t) {
        setMaxNodeId(f);
        setMaxNodeId(t);
        ASSERT(checkHasNode(f));
        ASSERT(checkHasNode(t));
        nextNode[f].push_back(t);
        nextNode[t].push_back(f);
    }


    virtual std::vector<int> getNextNode(int nodeId) const override {
        ASSERT(checkHasNode(nodeId));
        return nextNode[nodeId];
    }
};
