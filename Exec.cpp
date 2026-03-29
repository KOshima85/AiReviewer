#include "Exec.h"
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// exec:
// 与えられたシェルコマンドを実行し、その標準出力を文字列として返す。
// - 内部で popen を使ってプロセスを開き、fgets で出力を読み取る。
// - popen に失敗した場合は std::runtime_error を投げる。
// - 固定長バッファを使い、結果を result に追加して返す。
// 注意: 大きな出力やバイナリデータの扱いには注意が必要。
std::string exec(const std::string& cmd) {
    std::vector<char> buffer(4096);
    std::string result;
    result.reserve(8192);

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) throw std::runtime_error("popen() failed");

    while (true) {
        size_t n = std::fread(buffer.data(), 1, buffer.size(), fp);
        if (n > 0) result.append(buffer.data(), n);
        if (n < buffer.size()) {
            if (std::feof(fp)) break;
            if (std::ferror(fp)) {
                pclose(fp);
                throw std::runtime_error("Error reading from pipe");
            }
        }
    }

    int rc = pclose(fp);
    (void)rc; // 必要なら rc をチェックして例外化
    return result;
}