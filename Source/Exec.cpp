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
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#endif

// exec:
// 与えられたシェルコマンドを実行し、その標準出力を文字列として返す。
// - 内部で popen を使ってプロセスを開き、fread で出力を読み取る。
// - popen に失敗した場合は std::runtime_error を投げる。
// - 固定長バッファを使い、結果を result に追加して返す。
// - データが届かない状態が続いた際、timeout (ms) を超えたら警告を出して打ち切る。
//   timeout == 0 の場合はタイムアウトなし（プロセス終了まで待つ）。
// 注意: 大きな出力やバイナリデータの扱いには注意が必要。
std::string exec(const std::string& cmd, std::chrono::milliseconds timeout) {
    std::vector<char> buffer(4096);
    std::string result;
    result.reserve(8192);
#ifdef _DEBUG
	std::cout << "Executing command: " << cmd << std::endl;
#endif
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) throw std::runtime_error("popen() failed");

    // データが届かないときのポーリング間隔
    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    // アイドル（データなし）状態の累計時間
    std::chrono::milliseconds idleElapsed{0};

    while (true) {
        size_t n = std::fread(buffer.data(), 1, buffer.size(), fp);
        if (n > 0) {
            result.append(buffer.data(), n);
            idleElapsed = std::chrono::milliseconds{0}; // データ受信でアイドル時間をリセット
        }

        if (n < buffer.size()) {
            if (std::feof(fp)) break;
            if (std::ferror(fp)) {
                pclose(fp);
                throw std::runtime_error("Error reading from pipe");
            }
            // データが届いていない（一時的な停止）: 少し待ってリトライ
            std::this_thread::sleep_for(kPollInterval);
            idleElapsed += kPollInterval;

            // タイムアウトが設定されており、アイドル時間が上限を超えたら警告して打ち切る
            if (timeout.count() > 0 && idleElapsed >= timeout) {
                std::cerr << "Warning: exec() timed out after " << idleElapsed.count() << " ms with no data\n";
                // パイプを閉じてプロセスに終了シグナルを送る
                // pclose は内部でプロセスの終了を待機するため、先に pclose して抜ける
                pclose(fp);
                fp = nullptr;
                return result;
            }
            continue;
        }

        // n == buffer.size() の場合はバッファが満杯なのでそのまま読み続ける
    }

    if (fp) {
        int rc = pclose(fp);
        (void)rc; // 必要なら rc をチェックして例外化
    }
    return result;
}