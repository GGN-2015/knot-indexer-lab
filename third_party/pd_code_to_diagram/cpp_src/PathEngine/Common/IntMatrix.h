#pragma once

#include <vector>
#include <iomanip>
#include <iostream>

#include "AbstractIntMatrix.h"
#include "../../Utils/MyAssert.h"
#include "../../BorderDetect/IntMatrix2/IntMatrix2.h"

class IntMatrix: public AbstractIntMatrix {
private:
    std::vector<std::vector<int> > m_vec;
    int m_row;
    int m_col;

public:
    virtual ~IntMatrix() {}

    IntMatrix(int n, int m): m_row(n), m_col(m) {
        std::vector<int> one_line;
        for(int i = 0; i < m; i += 1) {
            one_line.push_back(0);
        }
        for(int i = 0; i < n; i += 1) {
            m_vec.push_back(one_line);
        }
    }


    IntMatrix& operator=(const IntMatrix& rhs) {
        this -> m_vec = rhs.m_vec;
        this -> m_row = rhs.m_row;
        this -> m_col = rhs.m_col;
        return (*this);
    }

    IntMatrix2 toIntMatrix2() const {
        IntMatrix2 ans(m_row, m_col);
        for(int i = 0; i < m_row; i += 1) {
            for(int j = 0; j < m_col; j += 1) {
                ans.setPos(i, j, getPos(i, j));
            }
        }
        return ans;
    }

    virtual int getPos(int i, int j) const override {
        if(0 <= i && i < m_row && 0 <= j && j < m_col) {
            return m_vec[i][j];
        }else {
            return 0;
        }
    }

    virtual void setPos(int i, int j, int v) override {
        ASSERT(0 <= i && i < m_row && 0 <= j && j < m_col);
        m_vec[i][j] = v;
    }

    virtual int getRowCnt() const override {
        return m_row;
    }

    virtual int getColCnt() const override {
        return m_col;
    }

    virtual void outputValue(std::ostream& out, int val) const {
        out << std::setw(3) << std::setfill(' ') << val << " ";
    }

    virtual void debugOutput(std::ostream& out, bool with_zero) const override {
        for(int i = 0; i < m_row; i += 1) {
            for(int j = 0; j < m_col; j += 1) {
                if(getPos(i, j) != 0) {
                    outputValue(out, getPos(i, j));
                }else {
                    if(with_zero) {
                        outputValue(out, 0);
                    }else {
                        out << "    ";
                    }
                }
            }
            out << std::endl;
        }
    }
};
