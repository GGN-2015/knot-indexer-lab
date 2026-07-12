#pragma once

#include <algorithm>
#include <tuple>

#include "PathEngine/GraphEngine/VectorGraphEngine.h"
#include "PathEngine/GraphEngine/PixelGraphEngine.h"
#include "PathEngine/GraphEngineWrap/ErasePointGraphEngineWrap.h"
#include "PathEngine/GraphEngineWrap/MergeGraphEngineWrap.h"
#include "PathEngine/GraphEngineWrap/SpanGraphEngineWrap.h"
#include "PathEngine/PathAlgorithm/SpfaPathEngine.h"
#include "PDTreeAlgo/SocketInfo.h"
#include "Utils/Coord2dPosition.h"
#include "Utils/MyAssert.h"

template<typename T>
void vecPushFront(std::vector<T>& vec, T&& value) {
    vec.insert(vec.begin(), std::forward<T>(value));
}


class LinkAlgo {
private:
    SocketInfo socket_info;
    VectorGraphEngine treeEdgeVGE;
    VectorGraphEngine crossingVGE;
    int crossing_cnt;

    void rawParsify(int k) {
        ASSERT(crossing_cnt > 0);
        auto c2ds1 = treeEdgeVGE.getCoord2dSet();
        auto c2ds2 = crossingVGE.getCoord2dSet();
        auto c2dsm = Coord2dSet::merge(c2ds1, c2ds2);

        socket_info.commitCoordMap(c2dsm, k);
        treeEdgeVGE.commitCoordMap(c2dsm, k);
        crossingVGE.commitCoordMap(c2dsm, k);
    }


    void parseArrange() {
        ASSERT(crossing_cnt > 0);
        rawParsify(6);
    }

    void saveOne(const std::vector<int>& unused_sokcet_id_list) {
        ASSERT(crossing_cnt > 0);
        auto socket_id = unused_sokcet_id_list[0];


        SpanGraphEngineWrap sgew(crossingVGE);
        MergeGraphEngineWrap megw(
            sgew,
            treeEdgeVGE
        );
        ErasePointGraphEngineWrap epgew(megw);


        auto vec = socket_info.getInfo(socket_id);
        ASSERT(vec.size() == 2);
        for(auto pos: vec) {
            int xpos, ypos;
            Direction dir;
            std::tie(xpos, ypos, dir) = pos;

            int dx = (int)round(Coord2dPosition::getDeltaPositionByDirection(dir).getX());
            int dy = (int)round(Coord2dPosition::getDeltaPositionByDirection(dir).getY());
            epgew.addEmptyPos(xpos, ypos);
            epgew.addEmptyPos(xpos + dx, ypos + dy);
        }


        int x1, y1; Direction d1; std::tie(x1, y1, d1) = vec[0];
        int x2, y2; Direction d2; std::tie(x2, y2, d2) = vec[1];


        int xmin, xmax, ymin, ymax;
        std::tie(xmin, xmax, ymin, ymax) = epgew.getBorderCoord();
        xmin -= 5;
        xmax += 5;
        ymin -= 5;
        ymax += 5;


        std::vector<LineData> path;
        if(x1 != x2 || y1 != y2) {
            auto pr = SpfaPathEngine().runAlgo(epgew, xmin, xmax, ymin, ymax, x1, y1, x2, y2);
            ASSERT(std::get<0>(pr) != -1.0);
            ASSERT(std::get<1>(pr).size() != 0);

            auto dis  = std::get<0>(pr);
            path = std::get<1>(pr);
        }else {



            PixelGraphEngine single_point_graph;
            single_point_graph.setPos(x1, y1, -3);
            MergeGraphEngineWrap nmgew(single_point_graph, epgew);
            ASSERT(nmgew.getPos(x1, y1) != 0);


            int new_x1, new_y1; Direction new_d1; std::tie(new_x1, new_y1, new_d1) = vec[0];
            int new_x2, new_y2; Direction new_d2; std::tie(new_x2, new_y2, new_d2) = vec[1];
            ASSERT(new_x1 == x1 && new_y1 == y1);
            ASSERT(new_x2 == x2 && new_y2 == y2);


            int dx1 = (int)round(Coord2dPosition::getDeltaPositionByDirection(new_d1).getX());
            int dy1 = (int)round(Coord2dPosition::getDeltaPositionByDirection(new_d1).getY());
            int dx2 = (int)round(Coord2dPosition::getDeltaPositionByDirection(new_d2).getX());
            int dy2 = (int)round(Coord2dPosition::getDeltaPositionByDirection(new_d2).getY());


            new_x1 += dx1;
            new_y1 += dy1;
            new_x2 += dx2;
            new_y2 += dy2;


            auto pr = SpfaPathEngine().runAlgo(nmgew, xmin, xmax, ymin, ymax, new_x1, new_y1, new_x2, new_y2);
            ASSERT(std::get<0>(pr) != -1.0);
            ASSERT(std::get<1>(pr).size() != 0);

            auto dis  = std::get<0>(pr);
            path = std::get<1>(pr);


            ASSERT(x1 == x2 && y1 == y2);
            vecPushFront(path, LineData(x1, path[0].getXf(), y1, path[0].getYf(), 0));
            path.push_back(LineData(path[path.size() - 1].getXt(), x1, path[path.size() - 1].getYt(), y1, 0));
        }


        for(const LineData& ld: path) {
            auto line_data_now = ld.setV(socket_id);
            treeEdgeVGE.setLine(line_data_now);
        }
        socket_info.setUsed(socket_id, true);
    }


    void compactArrange() {
        ASSERT(crossing_cnt > 0);
        rawParsify(2);
    }


    void buildOne() {
        ASSERT(crossing_cnt > 0);
        auto unused_sokcet_id_list = socket_info.getAllUnusedId(crossing_cnt);
        ASSERT(unused_sokcet_id_list.size() > 0);

        parseArrange();
        saveOne(unused_sokcet_id_list);
        compactArrange();
    }


    void buildAll() {
        ASSERT(crossing_cnt > 0);
        while(socket_info.getUsedCnt() < 2 * crossing_cnt) {
            buildOne();
        }
    }

public:
    ~LinkAlgo(){}


    LinkAlgo(int _crossing_cnt, const SocketInfo& _socket_info, int component_cnt): 
        socket_info(_socket_info), crossing_cnt(_crossing_cnt) {
        socket_info.check(_crossing_cnt, component_cnt);
        treeEdgeVGE = socket_info.getTreeEdgeVGE();
        crossingVGE = socket_info.getCrossingVGE();
        buildAll();
    }


    LinkAlgo() {
        crossing_cnt = -1;
    }



    MergeGraphEngineWrap getFinalGraph() {
        ASSERT(crossing_cnt > 0);
        return MergeGraphEngineWrap(
            crossingVGE,
            treeEdgeVGE
        );
    }



    std::vector<LineData> getAllEdges() const {
        ASSERT(crossing_cnt > 0);
        return treeEdgeVGE.getAllEdges();
    }
};
