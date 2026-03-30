#pragma once
#include <string>

// 文字列中の from を全て to に置換する
inline void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length(); // 無限ループ防止
    }
}
