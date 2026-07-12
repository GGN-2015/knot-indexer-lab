#pragma once

#ifndef DEBUG_BFS
    #define DEBUG_BFS (0)
#else
    #undef DEBUG_BFS
    #define DEBUG_BFS (1)
#endif

#include <queue>

#include "../IntMatrix2/ZeroOneMatrix.h"
#include "../IntMatrix2/BorderWrap.h"

#include "../../Utils/MyAssert.h"
#include "../../Utils/Debug.h"

class BfsAlgo {
public:


    ZeroOneMatrix search(const ZeroOneMatrix& graph, int xpos, int ypos) const {
        ASSERT(0 <= xpos && xpos < graph.getRcnt());
        ASSERT(0 <= ypos && ypos < graph.getCcnt());


        auto vis = ZeroOneMatrix(graph.getRcnt(), graph.getCcnt());



        auto new_graph = BorderWrap(1, 
            std::make_shared<ZeroOneMatrix>(graph));
        ASSERT(graph.getPos(xpos, ypos) == 0);


        std::queue<std::tuple<int, int>> q;
        q.push(std::make_tuple(xpos, ypos));
        vis.setPos(xpos, ypos, 1);

        const int dx[] = {1, -1, 0,  0};
        const int dy[] = {0,  0, 1, -1};

        while(!q.empty()) {
            auto [x, y] = q.front(); q.pop();
            SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_BFS, 
                std::string("  - Checking Position (") 
                    + std::to_string(x) + ", " + std::to_string(y) + ")");

            for(int d = 0; d < 4; d += 1) {
                int nx = x + dx[d];
                int ny = y + dy[d];


                SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_BFS, 
                    std::string("  - Checking Adj Position (") 
                        + std::to_string(nx) + ", " + std::to_string(ny) + ")");



                if(new_graph.getPos(nx, ny) || vis.getPos(nx, ny)) {
                    continue;
                }


                vis.setPos(nx, ny, 1);
                q.push(std::make_tuple(nx, ny));
            }
        }


        SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_BFS, "BFS Finished");
        return vis;
    }
};
