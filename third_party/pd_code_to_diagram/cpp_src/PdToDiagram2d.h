#pragma once
#include <algorithm>
#include <sstream>
#include <tuple>

#include "BorderDetect/BorderDetect.h"
#include "BorderDetect/Graph/ConnectedComponents.h"
#include "BorderDetect/Graph/Graph.h"
#include "PathEngine/Common/IntMatrix.h"
#include "PDTreeAlgo/PDCode.h"
#include "PDTreeAlgo/PDTree.h"
#include "Utils/Debug.h"
#include "Utils/Exceptions.h"
#include "Utils/Random.h"
#include "Utils/StringStream.h"
#include "LinkAlgo.h"

class PdToDiagram2d {
public:
    virtual std::tuple<LinkAlgo, IntMatrix> tryConvertOnce(
        unsigned int seed,
        int last_socket_id,
        std::stringstream& pd_code_ss
    ) const {
        

        myrandom::setSeed(seed);

        SHOW_DEBUG_MESSAGE("input pd_code ...");
        PDCode pd_code;
        pd_code.InputPdCode(pd_code_ss);

        SHOW_DEBUG_MESSAGE("generating pd_tree ...");
        PDTree pd_tree;


        while(true) {
            pd_tree.clear();
            pd_tree.load(pd_code, last_socket_id);
            if(pd_tree.checkNoOverlay()) {
                break;
            }
        }

        SHOW_DEBUG_MESSAGE("generating and checking socket_info ...");
        SocketInfo s_info = pd_tree.getSocketInfo();
        int component_cnt = pd_tree.getComponentCnt();
        s_info.check(pd_code.getCrossingNumber(), component_cnt);

        SHOW_DEBUG_MESSAGE("running link algo ...");
        LinkAlgo link_algo(pd_code.getCrossingNumber(), s_info, component_cnt);
        auto im = link_algo.getFinalGraph().exportToIntMatrix();


        SHOW_DEBUG_MESSAGE("checking border ...");
        auto im2 = im.toIntMatrix2();
        auto detector = BorderDetect();
        auto detector_flag = detector.checkBorderMaxCC(last_socket_id, im2);


        if(!detector_flag) {
            THROW_EXCEPTION(BadBorderException, "");
        }


        return std::make_tuple(link_algo, im);
    }



    virtual std::tuple<LinkAlgo, IntMatrix> convert(
        unsigned int min_seed, 
        int last_socket_id,
        std::stringstream& pd_code_ss,
        int max_try = 100
    ) const {
        auto ans = std::make_tuple(LinkAlgo(), IntMatrix(1, 1));

        bool fail = true;
        bool suc = false;
        for(unsigned int seed = min_seed; seed <= min_seed + max_try; seed += 1) {
            try{
                REWIND_STRING_STREAM(pd_code_ss);


                ans = tryConvertOnce(seed, last_socket_id, pd_code_ss);

                fail = false;
                suc = true;
            }
            PROCESS_EXCEPTION(CrossingMeetException, fail = true)
            PROCESS_EXCEPTION(BadBorderException, fail = true)

            if(!fail) {
                break;
            }
        }


        if(!suc) { 
            SHOW_DEBUG_MESSAGE(
                std::string("failed after ") 
                + std::to_string(max_try) + std::string(" try."));


            THROW_EXCEPTION(MaxTryExceeded, "");
        }
        return ans;
    }


    virtual std::vector<std::set<int>> getAllCc(std::stringstream& pd_code_ss) const {
        PDCode pd_code;
        pd_code.InputPdCode(pd_code_ss);


        Graph node_graph;
        for(int i = 0; i < pd_code.getCrossingNumber(); i += 1) {
            auto crossing = pd_code.getCrossing(i);
            for(int j = 0; j <= 1; j += 1) {
                int frm = crossing.getRaw(j);
                int eto = crossing.getRaw(j + 2);
                node_graph.addEdge(frm, eto);
            }
        }


        auto cc_alg = ConnectedComponents(node_graph);
        auto all_cc = cc_alg.getConnectedComponents();
        return all_cc;
    }
};
