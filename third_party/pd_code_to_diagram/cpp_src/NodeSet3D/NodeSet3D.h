#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <set>
#include <tuple>

#include "../Utils/MyAssert.h"


namespace std {
template <>
struct hash<std::tuple<int, int, int>> {
    using argument_type = std::tuple<int, int, int>;
    using result_type = size_t;

    result_type operator()(const argument_type& t) const noexcept {

        int a = std::get<0>(t);
        int b = std::get<1>(t);
        int c = std::get<2>(t);


        hash<int> hasher;
        size_t h1 = hasher(a);
        size_t h2 = hasher(b);
        size_t h3 = hasher(c);



        h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        h1 ^= h3 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        
        return h1;
    }
};

}
template<typename _T>
class NodeSet3D {
private:
    int node_cnt;
    std::unordered_map<int, std::tuple<_T, _T, _T>> id_to_coord;
    std::unordered_map<std::tuple<_T, _T, _T>, int> coord_to_id;


    std::set<std::tuple<int, int>> link_set;

public:
    NodeSet3D() {
        node_cnt = 0;
    }




    int addOrGetNodeId(_T x, _T y, _T z) {
        if(coord_to_id.count(std::make_tuple(x, y, z)) >= 1) {
            return getNodeId(x, y, z);
        }
        int new_id = ++ node_cnt;
        id_to_coord[new_id] = std::make_tuple(x, y, z);
        coord_to_id[std::make_tuple(x, y, z)] = new_id;
        return new_id;
    }


    int getNodeId(_T x, _T y, _T z) const {
        ASSERT(coord_to_id.count(std::make_tuple(x, y, z)) >= 1);
        return coord_to_id.find(std::make_tuple(x, y, z)) -> second;
    }


    void link(int id1, int id2) {
        ASSERT(id1 != id2);
        ASSERT(1 <= id1 && id1 <= node_cnt);
        ASSERT(1 <= id2 && id2 <= node_cnt);
        if(id1 > id2) std::swap(id1, id2);

        link_set.insert(std::make_tuple(id1, id2));
    }


    void outputGraph(std::ostream& out) const {

        out << "node_cnt " << node_cnt << "\n";
        out << "edge_cnt " << link_set.size() << "\n";

        for(int id = 1; id <= node_cnt; id += 1) {
            int x, y, z;
            std::tie(x, y, z) = id_to_coord.find(id) -> second;
            out << "node " << id << " pos " << x << " " << y << " " << z << "\n";
        }

        for(auto item: link_set) {
            out << "link " << std::get<0>(item) << " " << std::get<1>(item) << "\n";
        }
        out << std::endl;
    }
};
