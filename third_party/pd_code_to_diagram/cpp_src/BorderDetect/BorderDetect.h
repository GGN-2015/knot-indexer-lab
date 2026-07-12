#pragma once

#ifndef DEBUG_BORDER_DETECT
    #define DEBUG_BORDER_DETECT (0)
#else
    #undef DEBUG_BORDER_DETECT
    #define DEBUG_BORDER_DETECT (1)
#endif

#include <set>

#include "BFS/BfsAlgo.h"
#include "DataInput/FileDataInput.h"
#include "Graph/ConnectedComponents.h"
#include "Graph/DiagramGraph.h"
#include "IntMatrix2/BorderMask.h"
#include "IntMatrix2/ZeroOneMatrix.h"

#include "../Utils/MyAssert.h"
#include "../Utils/Debug.h"

class BorderDetect {
private:

    std::set<int> intersect(std::set<int> a, std::set<int> b) const {
        std::set<int> ans;
        for(auto item: a) {
            if(b.find(item) != b.end()) {
                ans.insert(item);
            }
        }
        return ans;
    }

public:


    virtual std::vector<std::set<int>> getAllCc(const IntMatrix2& imx) const {
        auto dg = DiagramGraph(imx);
        auto cc_alg = ConnectedComponents(dg);
        auto all_cc = cc_alg.getConnectedComponents();
        return all_cc;
    }


    virtual std::string jsonifyAllCc(const std::vector<std::set<int>>& all_cc) const {
        std::string json_string;

        bool first_line = true;
        json_string += "[\n";
        for(const auto& cc: all_cc) {
            if(first_line) {
                first_line = false;
            }else {
                json_string += ",\n";
            }
            bool first = true;
            for(const auto& item: cc) {
                if(first) {
                    first = false;
                    json_string += "    [";
                }else {
                    json_string += ", ";
                }
                json_string += std::to_string(item);
            }
            json_string += "]";
        }
        json_string += "\n]\n";
        return json_string;
    }






    virtual bool checkBorderMaxCC(int last_socket_id, const IntMatrix2& imx) const {


        auto mxv = imx.getMax(); 
        ASSERT(mxv > 0);

        int lastv = mxv;
        if(last_socket_id > 0) {
            lastv = last_socket_id;
        }


        auto mmx = ZeroOneMatrix(imx);



        SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_BORDER_DETECT, "Solving BFS");
        BfsAlgo bfs_algo;
        auto vis_mx = bfs_algo.search(mmx, 0, 0);


        SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_BORDER_DETECT, "Solving Border");
        auto border_pos_raw = BorderMask(std::make_shared<ZeroOneMatrix>(vis_mx), 0);
        auto border_pos     = ZeroOneMatrix(border_pos_raw);



        auto set_int = border_pos.select(imx);



        SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_BORDER_DETECT, "Solving Connected Component");
        auto dg = DiagramGraph(imx);
        auto cc_alg = ConnectedComponents(dg);
        auto all_cc = cc_alg.getConnectedComponents();


        auto lastv_cc = std::set<int>();
        for(const auto& cc: all_cc) {
            if(cc.find(lastv) != cc.end()) {
                lastv_cc = cc;
            }
        }






        SHOW_CERTAIN_DEBUG_MESSAGE(DEBUG_BORDER_DETECT, "Checking Cover");
        return intersect(lastv_cc, set_int).size() != 0;
    }
};
