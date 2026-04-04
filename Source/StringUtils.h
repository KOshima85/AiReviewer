#pragma once
#include <string>
#include <cctype>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

// コミット SHA として有効な文字列か検証する（省略形含む 4〜40 文字の hex）
// コマンドインジェクション対策として getCommitDiff/RunCommit に渡す前に呼ぶこと
inline bool isValidCommitSha(const std::string& sha)
{
    if (sha.size() < 4 || sha.size() > 40) return false;
    for (char c : sha) {
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

// glob パターンとして安全な文字のみで構成されているか検証する
// シェルコマンドへの注入を防ぐため、英数字と一般的な glob 記号のみを許容する
// 注意: []{}! はシェル/git pathspec で特殊な意味を持つため意図的に除外している
inline bool isValidGlobPattern(const std::string& pattern)
{
    if (pattern.empty()) return false;
    for (char c : pattern) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '*' && c != '?' && c != '.' && c != '_' && c != '-' &&
            c != '/') {
            return false;
        }
    }
    return true;
}

// stdout が ANSI カラーに対応しているか（TTY かどうか）を返す
// 結果は static にキャッシュして毎回システムコールしないようにする
inline bool isAnsiSupported()
{
#ifdef _WIN32
    static const bool s_supported = (_isatty(_fileno(stdout)) != 0);
#else
    static const bool s_supported = (isatty(fileno(stdout)) != 0);
#endif
    return s_supported;
}

// レビュー結果テキスト中の危険度ラベルに ANSI カラーコードを適用して返す
// HIGH   → 赤    (\033[31m)
// MEDIUM → 黄    (\033[33m)
// LOW    → 緑    (\033[32m)
// 将来的に HTML タグや JSON 形式への変換が必要な場合はここを変更する
inline std::string applySeverityColors(std::string text)
{
    if (!isAnsiSupported()) return text;
    replaceAll(text, "HIGH",   "\033[31mHIGH\033[0m");
    replaceAll(text, "MEDIUM", "\033[33mMEDIUM\033[0m");
    replaceAll(text, "LOW",    "\033[32mLOW\033[0m");
    return text;
}

// LLM などの外部出力に含まれる ANSI/VT エスケープシーケンスを除去する
// ターミナル操作（画面クリア・タイトル変更等）を悪用した攻撃を防ぐ
inline std::string stripAnsiEscapes(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\033' && i + 1 < text.size()) {
            char next = text[i + 1];
            if (next == '[') {
                // CSI シーケンス: ESC [ ... <終端バイト 0x40-0x7E>
                i += 2;
                while (i < text.size() && !(text[i] >= 0x40 && text[i] <= 0x7E)) ++i;
                if (i < text.size()) ++i;
            } else if (next == ']') {
                // OSC シーケンス: ESC ] ... BEL または ESC '\'
                i += 2;
                while (i < text.size()) {
                    if (text[i] == '\007') { ++i; break; }
                    if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '\\') { i += 2; break; }
                    ++i;
                }
            } else {
                // 2文字エスケープ: ESC <1文字>
                i += 2;
            }
        } else {
            result.push_back(text[i]);
            ++i;
        }
    }
    return result;
}
