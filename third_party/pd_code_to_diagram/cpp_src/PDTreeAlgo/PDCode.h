#pragma once

#include <istream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "NumInput.h"
#include "PDCrossing.h"
#include "../Utils/MyAssert.h"






class PDCode {
private:


    int n;


    std::vector<std::vector<int>> pd_code;


    void __dfs(
        const std::map<int, std::set<int>>& graph_next, 
        int socket_id, 
        std::set<int>& visit) const {
        

        if(visit.find(socket_id) != visit.end()) {
            return;
        }


        visit.insert(socket_id);


        for(const auto& item: (graph_next.find(socket_id)->second)) {
            __dfs(graph_next, item, visit);
        }
    }

public:



    std::map<int, std::set<int>> getGraphNext() const {
        auto graph_next = std::map<int, std::set<int>>();
        for(int i = 0; i < getCrossingNumber(); i += 1) {
            for(int d = 0; d <= 1; d += 1) {
                auto frm = pd_code[i][d + 0];
                auto eto = pd_code[i][d + 2];
                graph_next[frm].insert(eto);
                graph_next[eto].insert(frm);
            }
        }
        return graph_next;
    }


    std::set<int> getComponent(int socket_id) const {
        auto graph_next = getGraphNext();
        auto visit = std::set<int>();
        __dfs(graph_next, socket_id, visit);
        return visit;
    }


    void clear() {
        n = 0;
        pd_code.clear();
    }



    PDCode(): n(0), pd_code({}) {
    }



    int getCrossingNumber() const {
        return n;
    }


    void sanityCheck() const {
        

        ASSERT(getCrossingNumber() != 0);
    }


    PDCrossing getCrossing(int idx) const {
        sanityCheck();


        ASSERT(0 <= idx && idx < getCrossingNumber());
        

        PDCrossing pd_crossing;
        pd_crossing.load(pd_code[idx]);
        return pd_crossing;
    }


    std::string toString(
        std::string before_code="PD",
        std::string before_item="X",
        std::string begin_item="[", 
        std::string sep=", ", 
        std::string end_item="]") const {
        
        sanityCheck();
        std::string ans = before_code + begin_item;
        
        for(int i = 0; i < n; i += 1) {
            ans += getCrossing(i).toString(before_item, begin_item, sep, end_item);
            if(i != n-1) {
                ans += sep;
            }
        }
        return ans + end_item;
    }


    bool InputPdCode(std::istream& input_stream) {


        pd_code.clear();
        

        auto int_vec = extractIntegersFromStream(input_stream);
        ASSERT(int_vec.size() != 0);
        ASSERT(int_vec.size() % 4 == 0);
        n = int_vec.size() / 4;
        


        std::map<int, int> cnt;

        for(int i = 0; i < n; i += 1) {
            std::vector<int> pd_crossing;
            for(int j = 0; j < 4; j += 1) {
                int pos = i * 4 + j;
                pd_crossing.push_back(int_vec[pos]);
                cnt[int_vec[pos]] += 1;
            }
            pd_code.push_back(pd_crossing);
        }



        for(int i = 1; i <= 2*n; i += 1) {
            if(cnt[i] != 2) {
                return false;
            }
        }
        return true;
    }
};
