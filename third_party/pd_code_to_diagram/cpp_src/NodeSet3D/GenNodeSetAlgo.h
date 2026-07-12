#pragma once

#include <iostream>
#include <vector>

#include "NodeSet3D.h"
#include "../PathEngine/GraphEngine/AbstractGraphEngine.h"
#include "../Utils/MyAssert.h"

class GenNodeSetAlgo {
private:
    const AbstractGraphEngine& age;
    std::vector<LineData> line_data_list;
    NodeSet3D<int> node_set_3d;



    void linkEdge(LineData line_data) {
        int xf = line_data.getXf();
        int xt = line_data.getXt();
        int yf = line_data.getYf();
        int yt = line_data.getYt();
        int v  = line_data.getV ();
        ASSERT(xf == xt || yf == yt);
        if(v <= 0) return;
        if(xf == xt) {
            int xpos = xf;
            for(int ypos = std::min(yf, yt); ypos < std::max(yf, yt); ypos += 1) {
                if(age.getPos(xpos, ypos) == v && age.getPos(xpos, ypos + 1) == v) {
                    int node1 = node_set_3d.addOrGetNodeId(xpos, ypos, 0);
                    int node2 = node_set_3d.addOrGetNodeId(xpos, ypos + 1, 0);
                    node_set_3d.link(node1, node2);
                }
            }
        }else {
            int ypos = yf;
            for(int xpos = std::min(xf, xt); xpos < std::max(xf, xt); xpos += 1) {
                if(age.getPos(xpos, ypos) == v && age.getPos(xpos + 1, ypos) == v) {
                    int node1 = node_set_3d.addOrGetNodeId(xpos, ypos, 0);
                    int node2 = node_set_3d.addOrGetNodeId(xpos + 1, ypos, 0);
                    node_set_3d.link(node1, node2);
                }
            }
        }
    }


    void linkDown(std::tuple<int, int, int> pos) {
        int x, y, z;
        std::tie(x, y, z) = pos;
        auto node1 = node_set_3d.addOrGetNodeId(x, y, z);
        auto node2 = node_set_3d.addOrGetNodeId(x, y, z - 1);
        node_set_3d.link(node1, node2);
    }

    void link(std::tuple<int, int, int> pos1, std::tuple<int, int, int> pos2) {
        int x1, y1, z1; std::tie(x1, y1, z1) = pos1;
        int x2, y2, z2; std::tie(x2, y2, z2) = pos2;
        auto node1 = node_set_3d.addOrGetNodeId(x1, y1, z1);
        auto node2 = node_set_3d.addOrGetNodeId(x2, y2, z2);
        node_set_3d.link(node1, node2);
    }


    void buildNodeSet3D() {


        for(auto line_data: line_data_list) { 
            linkEdge(line_data);
        }
        


        for(auto crossing_coord2d: age.getAllNegPos()) {
            int xmid, ymid;
            std::tie(xmid, ymid) = crossing_coord2d;

            auto val = age.getPos(xmid, ymid);
            ASSERT(val == -1 || val == -2);

            const int dx = (val == -1) ? 0 : 1;
            const int dy = 1 - dx;
            auto pos1 = std::make_tuple(xmid - dx, ymid - dy, 1);
            auto pos2 = std::make_tuple(xmid +  0, ymid +  0, 1);
            auto pos3 = std::make_tuple(xmid + dx, ymid + dy, 1);


            linkDown(pos1);
            linkDown(pos3);
            link(pos1, pos2);
            link(pos2, pos3);


            auto pos4 = std::make_tuple(xmid - dy, ymid - dx, 0);
            auto pos5 = std::make_tuple(xmid +  0, ymid +  0, 0);
            auto pos6 = std::make_tuple(xmid + dy, ymid + dx, 0);
            link(pos4, pos5);
            link(pos5, pos6);
        }
    }
public:
    GenNodeSetAlgo(const AbstractGraphEngine& _age, std::vector<LineData> _line_data_list): 
        age(_age), line_data_list(_line_data_list) {
        buildNodeSet3D();
    }


    NodeSet3D<int> getNodeSet3D() const {
        return node_set_3d;
    }


    void outputGraph(std::ostream& out) const {
        node_set_3d.outputGraph(out);
    }
};
