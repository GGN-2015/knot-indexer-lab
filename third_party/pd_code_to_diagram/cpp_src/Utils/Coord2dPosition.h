#pragma once

#include <cmath>

#include "Direction.h"
#include "MyAssert.h"


using Coord2dType = double;


class Coord2dPosition {
private:
    Coord2dType x, y;

public:

    static constexpr Coord2dType EPS = 1e-7;


    Coord2dType getX() const {return x;}
    Coord2dType getY() const {return y;}


    Coord2dPosition() {
        x = y = 0;
    }


    Coord2dPosition(Coord2dType nx, Coord2dType ny): x(nx), y(ny) {}


    double len() const {
        return sqrt(x * x + y * y);
    }



    Coord2dPosition unit() const {


        if(x == 0 && y == 0) return Coord2dPosition();


        auto l = len();
        return Coord2dPosition(
            getX() / l,
            getY() / l
        );
    }



    static Coord2dPosition getDeltaPositionByDirection(Direction dir) {
        if(dir == Direction::EAST) {
            return Coord2dPosition(1, 0);

        }else if(dir == Direction::NORTH) {
            return Coord2dPosition(0, 1);

        }else if(dir == Direction::WEST) {
            return Coord2dPosition(-1, 0);
        
        }else if(dir == Direction::SOUTH) {
            return Coord2dPosition(0, -1);
        

        }else {
            ASSERT(false);
        }
    }


    static Coord2dPosition add(Coord2dPosition pos1, Coord2dPosition pos2) {
        return Coord2dPosition(pos1.x + pos2.x, pos1.y + pos2.y);
    }


    static Coord2dPosition sub(Coord2dPosition pos1, Coord2dPosition pos2) {
        return Coord2dPosition(pos1.x - pos2.x, pos1.y - pos2.y);
    }


    static Coord2dType dot(Coord2dPosition pos1, Coord2dPosition pos2) {
        return pos1.x * pos2.x + pos1.y * pos2.y;
    }


    static bool same(Coord2dPosition pos1, Coord2dPosition pos2) {
        return std::abs(pos1.x - pos2.x) < EPS && std::abs(pos1.y - pos2.y) < EPS ;
    }


    static Coord2dType distance(Coord2dPosition pos1, Coord2dPosition pos2) {
        return sub(pos1, pos2).len();
    }
};
