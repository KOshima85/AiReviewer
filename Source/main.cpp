#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <cerrno>

#include <thread>
#include <chrono>

using nlohmann::json;

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "No filesystem support: require <filesystem> or <experimental/filesystem>"
#endif

#include "Define.h"
#include "Config.h"
#include "OllamaConnector.h"
#include "AIReviewer.h"

// ディレクトリ存在確認
static bool ensureDirectoryExists(const std::string& dir) {
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        return fs::is_directory(dir, ec);
    }
	// ディレクトリが存在しない場合は作成する
    return fs::create_directories(dir, ec);
}

// printHeader:
// プログラム開始時に表示するヘッダを出力する
void printHeader() {
    std::cout << "=============================\n";
    std::cout << " AI Code Review\n";
    std::cout << "=============================\n\n";
}

int main(int argc, char* argv[]) {
    if (!ensureDirectoryExists(csDataDir)) {
        std::cerr << "Failed to create data directory: " << csDataDir << "\n";
        return 1;
    }

    // --history N オプションのパース
    int historyCount = 0; // 0 = staged モード（通常動作）
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--history") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --history requires a positive integer argument.\n";
                std::cerr << "Usage: aireviewr --history <N>\n";
                return 1;
            }
            try {
                historyCount = std::stoi(argv[++i]);
            } catch (...) {
                historyCount = 0;
            }
            if (historyCount <= 0) {
                std::cerr << "Error: --history requires a positive integer. Got: " << argv[i] << "\n";
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            std::cerr << "Usage: aireviewr [--history <N>]\n";
            return 1;
        }
    }

    // 設定読み込み（無ければデフォルトを書き出す）
    Config cfg = Config::LoadOrCreate(csDataDir + "/config.json");
    
    auto now = std::chrono::system_clock::now();

	std::unique_ptr<OllamaConnector> connector (new OllamaConnector(&cfg));
    std::unique_ptr<AIReviewer> reviewer(new AIReviewer(&cfg, *connector));

    int reviewExitCode = 0;
    try {
        printHeader();

		reviewer->Initialize();

        if (historyCount > 0) {
            // --history N モード: 直近 N コミットをレビュー
            reviewExitCode = reviewer->RunHistory(historyCount);
        } else {
            // 通常モード: staged diff をレビュー
            std::string response = reviewer->RunOnce();
            if(response.empty()) {
                return 0;
            }
            reviewExitCode = reviewer->AnalyzeResponse(response);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    // 実行時間を出力
	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed = end - now;
    std::cout << "Execution time: " << elapsed.count() << " seconds\n";
    return reviewExitCode;
}