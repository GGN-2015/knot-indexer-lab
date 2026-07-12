#pragma once

#include <memory>

#include "AbstractGraph.h"
#include "Graph.h"
#include "../IntMatrix2/AbstractIntMatrix2.h"
#include "../../Utils/MyAssert.h"

class DiagramGraph: public AbstractGraph {
private:
    Graph graph;

public:
    virtual ~DiagramGraph() {}
    DiagramGraph(const AbstractIntMatrix2& aim): graph() {
        const int dx[] = {0, -1,  0, 1};
        const int dy[] = {1,  0, -1, 0};
        for(int i = 1; i < aim.getRcnt() - 1; i += 1) {
            for(int j = 1; j < aim.getCcnt() - 1; j += 1) {
                if(aim.getPos(i, j) < 0) {
                    int arr[4];
                    for(int d = 0; d < 4; d += 1) {
                        arr[d] = aim.getPos(i + dx[d], j + dy[d]);
                        ASSERT(arr[d] > 0);
                        graph.setMaxNodeId(d);
                    }
                    graph.addEdge(arr[0], arr[2]);
                    graph.addEdge(arr[1], arr[3]);
                }
            }
        }
    }
    


    virtual int getMaxNodeId() const override {
        return graph.getMaxNodeId();
    }


    virtual bool checkHasNode(int nodeId) const override {
        return 1 <= nodeId && nodeId <= graph.getMaxNodeId();
    }


    virtual std::vector<int> getNextNode(int nodeId) const {
        return graph.getNextNode(nodeId);
    }
};
