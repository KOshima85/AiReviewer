#include "Exec.h"
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <thread>
#include <chrono>
#include <iostream>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define COUNT_LIMIT 1000 // *10 msec

// exec:
// 与えられたシェルコマンドを実行し、その標準出力を文字列として返す。
// - 内部で popen を使ってプロセスを開き、fread で出力を読み取る。
// - popen に失敗した場合は std::runtime_error を投げる。
// - 固定長バッファを使い、結果を result に追加して返す。
// - ループ中にデータが得られない場合は短時間スリープして CPU スピンを防止する。
// 注意: 大きな出力やバイナリデータの扱いには注意が必要。
std::string exec(const std::string& cmd, bool useLimit) {
    std::vector<char> buffer(4096);
    std::string result;
    result.reserve(8192);
#ifdef _DEBUG
	std::cout << "Executing command: " << cmd << std::endl;
#endif
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) throw std::runtime_error("popen() failed");

    // スリープ時間（データが得られないときに短時間待つ）
    constexpr auto kIdleSleep = std::chrono::milliseconds(10);
	int idleCount = 0;
    while (true) {
        size_t n = std::fread(buffer.data(), 1, buffer.size(), fp);
        if (n > 0) {
            result.append(buffer.data(), n);
            idleCount = 0; // データを受信したのでアイドルカウントをリセット
        }

        if (n < buffer.size()) {
            if (std::feof(fp)) break;
            if (std::ferror(fp)) {
                pclose(fp);
                throw std::runtime_error("Error reading from pipe");
            }
            // n < buffer.size() だが feof/ferror でない場合は一時的にデータが来ていない。
            // アイドルカウントを進め、上限を超えたらタイムアウト警告を出す。
            idleCount++;
            if (useLimit && idleCount > COUNT_LIMIT) {
                std::cerr << "Warning: no data received for " << (idleCount * kIdleSleep.count()) << " ms\n";
                break;
            }
            std::this_thread::sleep_for(kIdleSleep);
            continue;
        }

        // n == buffer.size() の場合はさらに読み続ける（バッファが満杯）
    }

    int rc = pclose(fp);
    (void)rc; // 必要なら rc をチェックして例外化
    return result;
}