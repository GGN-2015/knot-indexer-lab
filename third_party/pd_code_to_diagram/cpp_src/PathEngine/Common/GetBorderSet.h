#pragma once

#include <iostream>
#include <queue>
#include <set>
#include <tuple>

#include "IntMatrix.h"
#include "AbstractIntMatrix.h"
#include "../../Utils/MyAssert.h"

class GetBorderSet {
private:
    std::set<int> border_set;

public:
    std::set<int> getAns() const {
        return border_set;
    }

    void debugOutput(std::ostream& out) const {
        for(auto v: border_set) {
            out << v << " ";
        }
        out << std::endl;
    }

    GetBorderSet(const AbstractIntMatrix& aim) {
        IntMatrix vis(aim.getRowCnt(), aim.getColCnt());

        std::queue<std::tuple<int, int> > que;

        vis.setPos(0, 0, 1);
        que.push(std::make_tuple(0, 0));
        ASSERT(aim.getPos(0, 0) == 0);


        static const int dx[] = {0, 0, 1,-1};
        static const int dy[] = {1,-1, 0, 0};


        while(!que.empty()) {
            int posx, posy;
            std::tie(posx, posy) = que.front(); que.pop();

            for(int d = 0; d < 4; d += 1) {
                int newx = posx + dx[d];
                int newy = posy + dy[d];
                if(0 <= newx && newx < aim.getRowCnt() && 0 <= newy && newy < aim.getColCnt()) {
                    if(aim.getPos(newx, newy) != 0) {
                        continue;
                    }
                    if(vis.getPos(newx, newy) != 0) continue;


                    vis.setPos(newx, newy, 1);
                    que.push(std::make_tuple(newx, newy));
                }
            }
        }


        for(int i = 0; i < aim.getRowCnt(); i += 1) {
            for(int j = 0; j < aim.getColCnt(); j += 1) {
                if(aim.getPos(i, j) == 0) continue;

                int vcnt = 0;
                for(int d = 0; d < 4; d += 1) {
                    int x = i + dx[d];
                    int y = j + dy[d];
                    if(vis.getPos(x, y) != 0) {
                        vcnt += 1;
                    }
                }
                if(vcnt > 0) {
                    border_set.insert(aim.getPos(i, j));
                }
            }
        }
    }

};