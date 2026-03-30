#pragma once
#include <string>
#include <cctype>

// 文字列中の from を全て to に置換する
inline void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length(); // 無限ループ防止
    }
}

// テキスト内の pattern の出現回数を数える
inline int countOccurrences(const std::string& text, const std::string& pattern)
{
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        ++count;
        pos += pattern.length();
    }
    return count;
}

// glob パターンとして安全な文字のみで構成されているか検証する
// シェルコマンドへの注入を防ぐため、英数字と一般的な glob 記号のみを許容する
inline bool isValidGlobPattern(const std::string& pattern)
{
    if (pattern.empty()) return false;
    for (char c : pattern) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '*' && c != '?' && c != '.' && c != '_' && c != '-' &&
            c != '/' && c != '[' && c != ']' && c != '{' && c != '}' && c != '!') {
            return false;
        }
    }
    return true;
}

// レビュー結果テキスト中の危険度ラベルに ANSI カラーコードを適用して返す
// HIGH   → 赤    (\033[31m)
// MEDIUM → 黄    (\033[33m)
// LOW    → 緑    (\033[32m)
// 将来的に HTML タグや JSON 形式への変換が必要な場合はここを変更する
inline std::string applySeverityColors(std::string text)
{
    replaceAll(text, "HIGH",   "\033[31mHIGH\033[0m");
    replaceAll(text, "MEDIUM", "\033[33mMEDIUM\033[0m");
    replaceAll(text, "LOW",    "\033[32mLOW\033[0m");
    return text;
}
