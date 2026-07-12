#pragma once



enum class Direction {
    EAST = 0,
    NORTH = 1,
    WEST = 2,
    SOUTH = 3
};


namespace std {
    template<> struct hash<Direction> {
        size_t operator()(const Direction& d) const {
            return hash<int>()(static_cast<int>(d));
        }
    };
}