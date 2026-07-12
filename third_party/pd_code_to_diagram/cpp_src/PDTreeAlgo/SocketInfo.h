#pragma once

#include <map>
#include <vector>

#include "../Utils/Debug.h"
#include "../Utils/Direction.h"
#include "../Utils/MyAssert.h"
#include "../PathEngine/GraphEngine/VectorGraphEngine.h"

class SocketInfo {
private:
    std::map<int, int> socket_used;



    std::map<int, std::vector<std::tuple<int, int, Direction>>> socket_info;



    std::map<std::tuple<int, int>, Direction> crossing_base_direction;


    int checked;

public:
    ~SocketInfo(){}
    SocketInfo() {
        checked  = false;
    }


    void setBaseDirection(int x, int y, Direction d) {
        ASSERT(checked == false);
        crossing_base_direction[std::make_tuple(x, y)] = d;
    }


    Direction getBaseDirection(int x, int y) const {
        ASSERT(crossing_base_direction.find(std::make_tuple(x, y)) != crossing_base_direction.end());
        return crossing_base_direction.find(std::make_tuple(x, y)) -> second;
    }

    int getUsedCnt() const {
        int socket_used_cnt = 0;
        for(auto pr: socket_used) {
            if(pr.second) {
                socket_used_cnt += 1;
            }
        }
        return socket_used_cnt;
    }


    std::vector<int> getAllUnusedId(int crossing_cnt) const {
        std::vector<int> ans;
        for(int i = 1; i <= 2 * crossing_cnt; i += 1) {
            if(!getUsed(i)) {
                ans.push_back(i);
            }
        }
        return ans;
    }
    
    void setUsed(int socket_id, bool new_val) {
        socket_used[socket_id] = new_val;
    }


    void addInfo(int socket_id, int x, int y, Direction d) {
        ASSERT(checked == false);
        socket_info[socket_id].push_back(std::make_tuple(x, y, d));
    }


    bool getUsed(int socket_id) const {
        if(socket_used.find(socket_id) == socket_used.end()) {
            return false;
        }else {
            return socket_used.find(socket_id) -> second;
        }
    }


    std::vector<std::tuple<int, int, Direction>> getInfo(int socket_id) {
        ASSERT(socket_info[socket_id].size() == 2);
        return socket_info[socket_id];
    }



    void check(int n, int component_cnt) {
        if(checked) return;

        int used_cnt = 0;
        for(int i = 1; i <= 2 * n; i += 1) {
            if(socket_used[i] != false) {
                used_cnt += 1;
            }
            if(socket_info[i].size() != 2) {
                ASSERT(false);
            }
            int x1, y1; Direction d1; std::tie(x1, y1, d1) = socket_info[i][0];
            int x2, y2; Direction d2; std::tie(x2, y2, d2) = socket_info[i][1];


            ASSERT(crossing_base_direction.find(std::make_tuple(x1, y1)) != crossing_base_direction.end());
            ASSERT(crossing_base_direction.find(std::make_tuple(x2, y2)) != crossing_base_direction.end());
        }

        ASSERT(used_cnt == n - component_cnt);
        checked = true;
    }


    void commitCoordMap(Coord2dSet& coord2d_set, int k) {
        ASSERT(checked == true);

        SocketInfo new_socket_info;
        new_socket_info.socket_used = socket_used;
        new_socket_info.checked = false;
        
        for(const auto& vec: socket_info) {
            auto socket_id = vec.first;
            const auto& socket_pos_dir_list = vec.second;
            for(const auto& data: socket_pos_dir_list) {
                int xnow;
                int ynow;
                Direction dnow;
                std::tie(xnow, ynow, dnow) = data;


                auto new_xnow = coord2d_set.xkRank(xnow, k);
                auto new_ynow = coord2d_set.ykRank(ynow, k);
                new_socket_info.addInfo(socket_id, new_xnow, new_ynow, dnow);
            }
        }


        new_socket_info.crossing_base_direction.clear();
        for(auto crossing_pr: crossing_base_direction) {
            std::tuple<int, int> pr;
            Direction d; 
            std::tie(pr, d) = crossing_pr;


            int x = std::get<0>(pr);
            int y = std::get<1>(pr);

            auto xnew = coord2d_set.xkRank(x, k);
            auto ynew = coord2d_set.ykRank(y, k);
            new_socket_info.setBaseDirection(xnew, ynew, d);
        }


        new_socket_info.checked = true;
        *this = new_socket_info;
        ASSERT(checked == true);
    }


    VectorGraphEngine getTreeEdgeVGE() const {
        ASSERT(checked == true);
        VectorGraphEngine vge;

        for(auto idx: socket_used) {
            auto socket_id = idx.first;
            auto used = idx.second;
            if(used) {
                int x1, y1; Direction d1;
                std::tie(x1, y1, d1) = socket_info.find(socket_id) -> second[0];
                int x2, y2; Direction d2;
                std::tie(x2, y2, d2) = socket_info.find(socket_id) -> second[1];


                vge.setLine(LineData(x1, x2, y1, y2, socket_id));
            }
        }
        return vge;
    }


    VectorGraphEngine getCrossingVGE() const {
        ASSERT(checked == true);
        std::set<std::tuple<int, int>> coord2d_set;

        for(const auto& socket_info_pair: socket_info) {
            const auto& socket_id = std::get<0>(socket_info_pair);
            const auto& socket_list = std::get<1>(socket_info_pair);
            for(const auto& pos_dir_data: socket_list) {
                int xpos, ypos;
                Direction dir;
                std::tie(xpos, ypos, dir) = pos_dir_data;
                coord2d_set.insert(std::make_tuple(xpos, ypos));
            }
        }

        VectorGraphEngine vge;
        for(const auto& coord2d: coord2d_set) {
            int xnow, ynow;
            std::tie(xnow, ynow) = coord2d;




            int neg_val = -((int)(crossing_base_direction.find(coord2d) -> second) % 2 + 1);
            vge.setPos(xnow, ynow, neg_val);
        }
        return vge;
    }

    void debugOutput() const {
        std::cout << "checked: " << (int)checked << std::endl;
        std::cout << "socket_used: " << std::endl;
        for(auto pr: socket_used) {
            std::cout << "    " << pr.first << ": " << (int)pr.second << std::endl;
        }
        std::cout << "crossing_base_direction: " << std::endl;
        for(auto pr: crossing_base_direction) {
            std::cout << "    (" << std::get<0>(pr.first) << ", " << std::get<1>(pr.first) << "): " << (int)pr.second << std::endl;
        }
        std::cout << "socket_info: " << std::endl;
        for(auto pr: socket_info) {
            std::cout << "    " << pr.first << ": ";
            for(auto xyd: pr.second) {
                int x, y; Direction d; std::tie(x, y, d) = xyd;
                std::cout << " (" << x << ", " << y << ", " << (int)d << ") ";
            }
            std::cout << std::endl;
        }
    }
};
