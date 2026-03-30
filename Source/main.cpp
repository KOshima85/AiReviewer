/*
計画（擬似コード） - 日本語で詳細に記述
1. ヘッダとプラットフォーム互換部分の準備
   - 必要な <iostream>, <sstream>, <fstream>, <cstdio>, <array>, <memory> をインクルード
   - Windows の場合は popen/_popen, pclose/_pclose のマクロ定義を行う

2. exec 関数
   - コマンドを受け取り、popen で子プロセスを開く
   - 固定サイズバッファ（128 バイト）を使って出力を読み取り result に連結
   - popen に失敗したら例外を投げる
   - 読み取り終わったら結果を返す

3. getGitDiff 関数
   - exec("git diff --staged") を呼び出して差分文字列を取得して返す

4. buildPrompt 関数
   - std::stringstream を使ってプロンプトを組み立てる
   - レビューのフォーカス項目（メモリ安全性、UB、例外安全性、性能、可読性）を挿入
   - severity の要求を追加し、git diff を埋め込む

5. callOllama 関数
   - 一時ファイル payload.json を .aireviewr に書き出す
   - curl コマンドを組み立ててローカルの Ollama API に渡す
   - exec で curl を実行して結果を取得して返す

6. printHeader 関数
   - ヘッダを標準出力に出す

7. main 関数の流れ
   - ヘッダを出力
   - staged な git diff を収集
   - 変更が無ければ早期リターン
   - プロンプトを構築し、Ollama に送信
   - 受け取った応答を表示
   - 例外は捕捉して標準エラーに表示

注記:
 - このファイルのコメントは日本語で詳細に説明しています。
 - 実運用では外部コマンドの実行や一時ファイル操作の安全性（競合、注入、権限）をさらに強化してください。
 - C++20 準拠でコンパイル可能なコードを維持します。
*/

#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <array>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <cerrno>

#include <thread>
#include <chrono>
#include <stdio.h>

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

int main() {
    if (!ensureDirectoryExists(csDataDir)) {
        std::cerr << "Failed to create data directory: " << csDataDir << "\n";
        return 1;
    }

    // 設定読み込み（無ければデフォルトを書き出す）
    Config cfg = Config::load_or_create(csDataDir + "/config.json");
    
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::string timestamp = std::to_string(now_c);

	std::unique_ptr<OllamaConnector> connector (new OllamaConnector(&cfg));  
    std::unique_ptr<AIReviewer> reviewer(new AIReviewer(&cfg, *connector));

    try {
        printHeader();

		reviewer->Initialize();
        std::string response = reviewer->RunOnce();
        if(response.empty()) {
			// 変更が無い場合は空文字列が返る想定なので、そのまま終了する
            return 0;
		}
        reviewer->AnalyzeResponse(response);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    // 実行時間を出力
	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed = end - now;
    std::cout << "Execution time: " << elapsed.count() << " seconds\n";
    return 0;
}