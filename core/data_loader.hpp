#pragma once
#include <vector>
#include <fstream>
#include <sstream>
#include "types.hpp"

// 加载 CSV 格式 K线，列顺序：ts,open,high,low,close,volume,amount
inline std::vector<Bar> LoadBars(const std::string& path) {
    std::vector<Bar> bars;
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);                    // 跳过表头
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        Bar b; char c;
        iss >> b.ts >> c >> b.open >> c >> b.high >> c >> b.low
            >> c >> b.close >> c >> b.volume >> c >> b.amount;
        if (!iss.fail()) bars.push_back(b);
    }
    return bars;
}