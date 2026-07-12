#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "../../Utils/Coord2dPosition.h"
#include "../../Utils/Direction.h"
#include "../../Utils/MyAssert.h"

#include "AbstractPathAlgorithm.h"
#include "../GraphEngine/AbstractGraphEngine.h"
#include "../GraphEngineWrap/MarginGraphEngineWrap.h"


namespace std {
    template<> struct hash<std::tuple<int, int, Direction>> {
        size_t operator()(const std::tuple<int, int, Direction>& t) const {
            auto h1 = hash<int>()(std::get<0>(t));
            auto h2 = hash<int>()(std::get<1>(t));
            auto h3 = hash<Direction>()(std::get<2>(t));
            
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}



class SpfaPathEngine: public AbstractPathAlgorithm {
private:
    using PosType = std::tuple<int, int, Direction>; 
    std::unordered_map<PosType,  double> dis;
    std::unordered_map<PosType, PosType> pre;
    std::unordered_set<PosType> vis;

    using NxtInfo = std::tuple<int, int, Direction, double>;
    std::vector<NxtInfo> getNextPos(PosType pos_at) const {
        std::vector<NxtInfo> ans;

        int xnow, ynow;
        Direction dnow;
        std::tie(xnow, ynow, dnow) = pos_at;

        auto coord2d = Coord2dPosition::getDeltaPositionByDirection(dnow);
        auto dx = (int)std::round(coord2d.getX());
        auto dy = (int)std::round(coord2d.getY());



        ans.push_back(std::make_tuple(xnow + dx, ynow + dy, dnow, 1.0));



        for(auto dnxt: {Direction::EAST, Direction::NORTH, Direction::WEST, Direction::SOUTH}) {
            if(dnxt == dnow) continue;
            ans.push_back(std::make_tuple(xnow, ynow, dnxt, 0.1));
        }
        return ans;
    }


    std::vector<LineData> getVecLineData(std::vector<PosType> path) const {
        std::vector<LineData> ans;
        for(int i = 0; i < path.size(); i += 1) {
            auto xnow = std::get<0>(path[i]);
            auto ynow = std::get<1>(path[i]);
            if(i == 0 || std::get<2>(path[i-1]) != std::get<2>(path[i])) {
                ans.push_back(LineData(xnow, xnow, ynow, ynow, 0));
            }else {
                ans[ans.size() - 1] = ans[ans.size() - 1].setAimPos(xnow, ynow);
            }
        }
        return ans;
    }
public:
    virtual ~SpfaPathEngine(){}

    virtual 
    std::tuple<double, std::vector<LineData>>
    runAlgo(const AbstractGraphEngine& age,
        int xmin, int xmax, int ymin, int ymax,
        int xf, int yf, int xt, int yt) override {


        dis.clear();
        vis.clear();


        if(xf == xt && yf == yt) {
            std::cerr << "warning: begin and end at same point" << std::endl;
            return std::make_tuple(
                0.0,
                std::vector<LineData>({LineData(xf, xt, yf, yt, 0)})
            );
        }
        





        auto gew = MarginGraphEngineWrap(age, xmin, xmax, ymin, ymax, -3, xf, yf, xt, yt);
        

        std::queue<PosType> q;
        for(auto dir: {Direction::EAST, Direction::NORTH, Direction::WEST, Direction::SOUTH}) {
            auto pos_at = std::make_tuple(xf, yf, dir);
            dis[pos_at] = 0;
            vis.insert(pos_at);
            q.push(pos_at);
        }

        while(!q.empty()) {
            auto pos_at = q.front(); q.pop();
            vis.erase(pos_at);


            auto xnow = std::get<0>(pos_at);
            auto ynow = std::get<1>(pos_at);
            auto dnow = std::get<2>(pos_at);
            auto distance_now = dis.find(pos_at) -> second;


            for(auto x_y_d_v: getNextPos(pos_at))
            {
                int xnxt, ynxt;
                Direction dnxt;
                double vnxt;
                std::tie(xnxt, ynxt, dnxt, vnxt) = x_y_d_v;

                if(gew.getPos(xnxt, ynxt) != 0) {
                    continue;
                }
                auto pos_nxt = std::make_tuple(xnxt, ynxt, dnxt);
                if(dis.find(pos_nxt) == dis.end() || dis[pos_nxt] > distance_now + vnxt) {

                    dis[pos_nxt] = distance_now + vnxt;
                    pre[pos_nxt] = pos_at;
                    if(vis.count(pos_nxt) == 0) {
                        vis.insert(pos_nxt);
                        q.push(pos_nxt);
                    }
                }
            }
        }

        double dis_now = std::numeric_limits<double>::infinity();
        PosType pos_now = std::make_tuple(xt, yt, Direction::EAST);
        for(auto dir: {Direction::EAST, Direction::NORTH, Direction::WEST, Direction::SOUTH}) {
            PosType pos = std::make_tuple(xt, yt, dir);
            if(dis.find(pos) != dis.end()) {
                if(dis.find(pos) -> second < dis_now) {
                    dis_now = dis.find(pos) -> second;
                    pos_now = pos;
                }
            }
        }

        if(std::isinf(dis_now)) {
            return std::make_tuple(-1.0, std::vector<LineData>({}));
        }


        auto old_pos_now = pos_now;
        std::vector<PosType> arr;
        while(true) {
            arr.push_back(pos_now);
            if(pre.find(pos_now) == pre.end()) {
                break;
            }else {
                pos_now = pre[pos_now];
            }
        }
        std::reverse(arr.begin(), arr.end());
        ASSERT(arr.size() >= 1);
        return std::make_tuple(
            dis[old_pos_now], getVecLineData(arr));
    }
};
