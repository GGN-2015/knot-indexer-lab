#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "AbstractDataInput.h"
#include "../../Utils/MyAssert.h"

class FileDataInput: public AbstractDataInput{
private:


    std::string trim(const std::string& str) const {

        auto start = str.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) {
            return "";
        }

        auto end = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(start, end - start + 1);
    }



    std::pair<int, std::vector<std::string>> count_non_empty_lines(std::istream& is) const {
        std::vector<std::string> non_empty_lines;
        std::string line;
        int count = 0;


        while (std::getline(is, line)) {

            std::string trimmed_line = trim(line);
            if (!trimmed_line.empty()) {
                non_empty_lines.push_back(line);
                count++;
            }
        }


        if (is.bad()) {
            throw std::runtime_error("fatal error while reading input stream");
        }

        return {count, non_empty_lines};
    }







    int count_non_blank_segments(const std::string& line) const {
        int count = 0;
        bool in_segment = false;


        for (char c : line) {

            if (std::isspace(static_cast<unsigned char>(c))) {

                in_segment = false;
            } else {

                if (!in_segment) {
                    count++;
                    in_segment = true;
                }

            }
        }

        return count;
    }

public:
    virtual ~FileDataInput() {}
    virtual IntMatrix2 loadMatrix(std::istream& in) const override {
        auto [row_cnt, vec] = count_non_empty_lines(in);
        ASSERT(row_cnt > 0);

        auto col_cnt = count_non_blank_segments(vec[0]);
        auto int_matrix = IntMatrix2(row_cnt, col_cnt);

        for(int i = 0; i < row_cnt; i += 1) {
            std::stringstream ss;
            ss << vec[i];
            for(int j = 0; j < col_cnt; j += 1) {
                int v; ss >> v;
                int_matrix.setPos(i, j, v);
            }
        }

        return int_matrix;
    }
};
