#pragma once

#include <string>
#include <vector>

#include "../Utils/Direction.h"
#include "../Utils/MyAssert.h"



class PDCrossing {
private:
    std::vector<int> crs;

public:

    void load(std::vector<int> crs) {
        this -> crs = crs;


        ASSERT(crs.size() == 4);
    }


    const std::vector<int> getRaw() const {
        return crs;
    }


    int getRaw(int idx) const {
        ASSERT(0 <= idx && idx < crs.size());
        return crs[idx];
    }


    void sanityCheck() const {


        ASSERT(crs.size() != 0);


        ASSERT(crs.size() == 4);
    }


    bool hasSocket(int socket_id) const {
        sanityCheck();


        for(int i = 0; i < 4; i += 1) {
            if(crs[i] == socket_id) {
                return true;
            }
        }
        return false;
    }


    std::string toString(
        std::string before_item="X",
        std::string begin_item="[", 
        std::string sep=", ", 
        std::string end_item="]") const {
        
        std::string ans = "";
        ans += before_item + begin_item;

        for(int i = 0; i < 4; i += 1) {
            ans += std::to_string(crs[i]);
            if(i != 3) {
                ans += sep;
            }
        }
        return ans + end_item;
    }



    int getSocketIdByDirection(Direction base, Direction aim) const {

        int delta_dir = ((4 + (int)aim - (int)base) % 4);
        ASSERT(0 <= delta_dir && delta_dir < 4);
        return crs[delta_dir];
    }




    Direction baseShift(int socket_id, Direction aim_dir) const {
        ASSERT(hasSocket(socket_id));

        int pos = -1;
        for(int i = 0; i < 4; i += 1) {
            if(crs[i] == socket_id) {
                pos = i;
                break;
            }
        }
        return (Direction)((4 + (int)aim_dir - pos) % 4);
    }
};
