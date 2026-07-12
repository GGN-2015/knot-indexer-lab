#pragma once

#include <algorithm>
#include <iostream>
#include <set>
#include <vector>

#include "../Utils/Coord2dPosition.h"
#include "../Utils/Direction.h"
#include "../Utils/MyAssert.h"
#include "../Utils/Exceptions.h"
#include "../Utils/Random.h"

#include "PDCrossing.h"
#include "PDCode.h"
#include "SocketInfo.h"



struct TreeNode {
    int next_node[4] {};
    Coord2dPosition pos2d;


    int getIdOnDirection(Direction dir) const {
        return next_node[(int)dir];
    }
};




struct TreeMsg {
    PDCrossing pd_crossing;
    Direction base_direction;
};



class PDTree {
private:
    PDCode pd_code;


    int component_cnt;



    std::vector<TreeNode> structure;
    std::vector<TreeMsg> message;


    std::map<int, int> socket_used;


    int newTreeNode() {
        structure.push_back(TreeNode());
        message.push_back(TreeMsg());
        return structure.size() - 1;
    }


    PDCrossing popRandomCrossing(std::vector<PDCrossing>& unused_list) {
        ASSERT(unused_list.size() != 0);

        int pos = myrandom::randomInt(0, unused_list.size() - 1);
        PDCrossing ans = unused_list[pos];
        unused_list.erase(unused_list.begin() + pos);
        return ans;
    }




    PDCrossing popCrossingBySocketId(std::vector<PDCrossing>& unused_list, int socket_id) {
        ASSERT(unused_list.size() != 0);

        int pos = -1;
        for(int i = 0; i < unused_list.size(); i += 1) {
            if(unused_list[i].hasSocket(socket_id)) {
                pos = i;
                break;
            }
        }


        ASSERT(pos != -1);


        PDCrossing ans = unused_list[pos];
        unused_list.erase(unused_list.begin() + pos);
        return ans;
    }


    struct LeafInfo {
        int node_id;
        Direction dir;
        int socket_id;



        double right = 0;
    };

    double getPositionPunish() const {
        const auto N = (pd_code.getCrossingNumber() + 1);
        return N * N * N;
    }




    double calcPositionPunish(Coord2dPosition new_pos) const {


        for(int i = 1; i < structure.size(); i += 1) {
            if(Coord2dPosition::same(structure[i].pos2d, new_pos)) {
                return getPositionPunish();
            }
        }


        return 0;
    }



    double calcSocketIdPunish(int socket_id, const std::set<int>& last_socket_component) const {
        const auto N = (pd_code.getCrossingNumber() + 1);


        if(last_socket_component.find(socket_id) != last_socket_component.end()) {
            return N;
        }else {
            return 0;
        }
    }





    double calcNearPunish(int fa_id, Coord2dPosition new_pos) const {
        const auto N = (pd_code.getCrossingNumber() + 1);


        for(int i = 1; i < structure.size(); i += 1) {


            if(i == fa_id) {
                continue;
            }
            if(Coord2dPosition::distance(structure[i].pos2d, new_pos) <= 1 + Coord2dPosition::EPS) {
                return 2 * N;
            }
        }


        return 0;
    }





    void dfs(int x, int fa, std::map<int, LeafInfo>& leaf_id_map, const std::set<int>& last_socket_component) {
        for(int i = 0; i < 4; i += 1) {
            if(structure[x].next_node[i] == fa) {
                continue;
            }
            if(structure[x].next_node[i] == 0) {
                int socket_id = message[x].pd_crossing.getSocketIdByDirection(
                    message[x].base_direction, (Direction)i);
                
                if(leaf_id_map.find(socket_id) != leaf_id_map.end()) {
                    


                    leaf_id_map.erase(leaf_id_map.lower_bound(socket_id));
                }else {



                    auto new_pos = Coord2dPosition::add(
                            structure[x].pos2d,
                            Coord2dPosition::getDeltaPositionByDirection((Direction)i));



                    leaf_id_map[socket_id] = (LeafInfo){
                        .node_id = x,
                        .dir = (Direction)i,
                        .socket_id = socket_id,







                        .right = (
                            structure[x].pos2d.len() + 
                            Coord2dPosition::dot(
                                structure[x].pos2d.unit(),
                                Coord2dPosition::getDeltaPositionByDirection((Direction)i)) * 0.5
                            - calcPositionPunish(new_pos)
                            - calcNearPunish(x, new_pos)
                            - calcSocketIdPunish(socket_id, last_socket_component))
                    };
                }

            }else {
                dfs(structure[x].next_node[i], x, leaf_id_map, last_socket_component);
            }
        }
    }
    



    static bool sortLeafList(LeafInfo li1, LeafInfo li2) {
        return li1.right > li2.right;
    }





    LeafInfo getBestSocket(int root, const std::map<int, LeafInfo>& leaf_id_map) {
        ASSERT(root >= 0);
        ASSERT(leaf_id_map.size() > 0);


        std::vector<LeafInfo> leaf_id_list;
        for(auto pr: leaf_id_map) {
            leaf_id_list.push_back(pr.second);
        }


        std::sort(leaf_id_list.begin(), leaf_id_list.end(), PDTree::sortLeafList);
        return leaf_id_list[0];
    }

    std::tuple<bool, std::map<int, LeafInfo>> checkGetSocket(int root, const std::set<int>& last_socket_component) {
        if(root <= -1) {
            return std::make_tuple(false, std::map<int, LeafInfo>());
        }
        ASSERT(root >= 0);




        std::map<int, LeafInfo> leaf_id_map;
        dfs(root, -1, leaf_id_map, last_socket_component);


        return std::make_tuple(leaf_id_map.size() > 0, leaf_id_map);
    }




    void buildTree(int last_socket_id) {

        const int n = pd_code.getCrossingNumber();


        const int max_socket_id = 2 * n;
        if(last_socket_id <= 0) {
            last_socket_id = max_socket_id;
        }



        auto last_socket_component = pd_code.getComponent(last_socket_id);
        ASSERT(n != 0);


        std::vector<PDCrossing> unused;
        for(int i = 0; i < n; i += 1) unused.push_back(pd_code.getCrossing(i));


        int root = -1;
        int used_crossing_cnt = 0;
        component_cnt = 0;


        while(unused.size() > 0) {




            auto pr = checkGetSocket(root, last_socket_component);
            if(!std::get<0>(pr)) {


                root = newTreeNode();
                message[root].pd_crossing = popRandomCrossing(unused);
                message[root].base_direction = Direction::EAST;
                structure[root].pos2d = Coord2dPosition(used_crossing_cnt + 1, used_crossing_cnt + 1);
                component_cnt += 1;

            }else {



                LeafInfo leaf_info = getBestSocket(root, std::get<1>(pr));


                if(!(leaf_info.right >= - 0.5 * getPositionPunish())) {
                    THROW_EXCEPTION(CrossingMeetException, "");
                }



                int new_node = newTreeNode();
                message[new_node].pd_crossing = popCrossingBySocketId(unused, leaf_info.socket_id);


                auto oppo_dir = (Direction)((2 + (int)leaf_info.dir)% 4);




                message[new_node].base_direction = message[new_node].pd_crossing.baseShift(
                    leaf_info.socket_id, oppo_dir);
                


                structure[new_node].next_node[(int)oppo_dir] = leaf_info.node_id;
                structure[leaf_info.node_id].next_node[(int)leaf_info.dir] = new_node;
                structure[new_node].pos2d = Coord2dPosition::add(
                    structure[leaf_info.node_id].pos2d, 
                    Coord2dPosition::getDeltaPositionByDirection(leaf_info.dir));


                socket_used[leaf_info.socket_id] = true;
            }


            used_crossing_cnt += 1;
        }
    }

public:

    int getComponentCnt() const {
        ASSERT(pd_code.getCrossingNumber() >= 1);
        return component_cnt;
    }
    

    void clear() {
        pd_code.clear();
        structure.clear();
        message.clear();



        int zero = newTreeNode();
        ASSERT(zero == 0);
    }




    void load(PDCode new_pd_code, int last_socket_id) {
        clear();

        pd_code = new_pd_code;
        ASSERT(pd_code.getCrossingNumber() != 0);

        buildTree(last_socket_id);
    }




    bool checkNoOverlay() const {
        ASSERT(pd_code.getCrossingNumber() > 0);


        std::set<std::tuple<int, int>> coord2d_set;
        for(int i = 1; i < structure.size(); i += 1) {
            int xnow = (int)round(structure[i].pos2d.getX());
            int ynow = (int)round(structure[i].pos2d.getY());
            coord2d_set.insert(std::make_tuple(xnow, ynow));
        }


        return coord2d_set.size() == pd_code.getCrossingNumber();
    }


    SocketInfo getSocketInfo() {
        ASSERT(pd_code.getCrossingNumber() > 0);
        SocketInfo socket_info;


        int n = pd_code.getCrossingNumber();
        for(int i = 1; i <= 2 * n; i += 1) {
            if(socket_used[i]) socket_info.setUsed(i, true);
        }


        for(int i = 1; i < message.size(); i += 1) {
            for(int d = 0; d < 4; d += 1) {
                auto socket_id = message[i].pd_crossing.getSocketIdByDirection(
                    message[i].base_direction, (Direction)d);
                int xpos = (int)std::round(structure[i].pos2d.getX());
                int ypos = (int)std::round(structure[i].pos2d.getY());


                socket_info.addInfo(socket_id, xpos, ypos, (Direction)d);
            }
            socket_info.setBaseDirection(
                (int)round(structure[i].pos2d.getX()),
                (int)round(structure[i].pos2d.getY()),
                message[i].base_direction);
        }
        return socket_info;
    }
    

    void debugOutput() {
        for(int i = 1; i < message.size();  i += 1) {
            std::cout << "node " << i << ": " << std::endl;



            std::cout << "    sockets(" << (int)message[i].base_direction << "): ";
            for(int d = 0; d < 4; d += 1) {
                std::cout << message[i].pd_crossing.getSocketIdByDirection(
                    message[i].base_direction,
                    (Direction)d
                ) << " ";
            }
            std::cout << std::endl;


            std::cout << "    tree: ";
            for(int d = 0; d < 4; d += 1) {
                std::cout << structure[i].next_node[d] << " ";
            }
            std::cout << std::endl;
        }
    }
};
