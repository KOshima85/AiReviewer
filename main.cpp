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

#define MODEL_NAME "gemma3:4b"

static const std::string csDataDir = ".aireviewr";

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "No filesystem support: require <filesystem> or <experimental/filesystem>"
#endif

#include "PayloadFile.h"

// ----- Config: 設定ファイルの読み書きと構造 -----
struct Config {
    std::string endpoint; // 例: "http://localhost"
    int port;             // 例: 11434
    std::string model;    // 例: "gemma3:4b"
    std::vector<std::string> review_focus; // レビューの焦点

    static Config defaults() {
        return Config{
            "http://localhost",
            11434,
            MODEL_NAME,
            { "メモリ安全性", "未定義動作", "例外安全性", "性能", "可読性","SOLID原則"}
        };
    }

    // 指定パスから読み込み。存在しなければデフォルトを書き出して返す。
    static Config load_or_create(const std::string& path) {
        Config cfg = defaults();
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            // ディレクトリは既に ensure_directory_exists で作られている想定
            // デフォルトを書き出す
            json j;
            j["endpoint"] = cfg.endpoint;
            j["port"] = cfg.port;
            j["model"] = cfg.model;
            j["review_focus"] = cfg.review_focus;
            std::ofstream out(path, std::ios::binary);
            if (out) out << j.dump(2);
            return cfg;
        }

        // ファイルがあれば読み込み、必要箇所を検証して採用する
        try {
            std::ifstream in(path, std::ios::binary);
            if (!in) return cfg;
            json j;
            in >> j;
            if (j.contains("endpoint") && j["endpoint"].is_string()) cfg.endpoint = j["endpoint"].get<std::string>();
            if (j.contains("port") && (j["port"].is_number_integer() || j["port"].is_string())) {
                if (j["port"].is_number_integer()) cfg.port = j["port"].get<int>();
                else {
                    try { cfg.port = std::stoi(j["port"].get<std::string>()); } catch (...) {}
                }
            }
            if (j.contains("model") && j["model"].is_string()) cfg.model = j["model"].get<std::string>();
            if (j.contains("review_focus") && j["review_focus"].is_array()) {
                cfg.review_focus.clear();
                for (auto &it : j["review_focus"]) {
                    if (it.is_string()) cfg.review_focus.push_back(it.get<std::string>());
                }
                if (cfg.review_focus.empty()) cfg.review_focus = defaults().review_focus;
            }
        } catch (...) {
            // 読み込み/解析失敗はデフォルトを返す（既存ファイルは上書きしない）
        }
        return cfg;
    }
};

// ディレクトリ作成（存在すれば OK）
static bool ensure_directory_exists(const std::string& dir) {
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        return fs::is_directory(dir, ec);
    }
    return fs::create_directories(dir, ec);
}

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

// getGitDiff:
// ステージされた変更（--staged）の git diff を取得するラッパー
// 改行の違いを無視する(-w)
std::string getGitDiff() {
    return exec("git diff --staged -w");
}

// buildPrompt:
// AI に送るプロンプトを組み立てる。
// - レビューの焦点（メモリ安全性、未定義動作、例外安全性、性能、可読性）を明示する。
// - 各問題に対して重大度（HIGH/MEDIUM/LOW）を返すよう指示する。
// - git diff を末尾に含める。
std::string buildPrompt(const std::string& diff, const std::vector<std::string>& focus) {
    std::stringstream prompt;
    prompt << "あなたは上級のC++エンジニアです。\n";
    prompt << "以下の git diff をレビューしてください。\n";
    prompt << "レビューの焦点:\n";
    for (const auto &f : focus) {
        prompt << "- " << f << "\n";
    }
    prompt << "\n各問題について危険度を (HIGH/MEDIUM/LOW) で示してください。\n\n";
    prompt << "問題は箇条書きしてください。\n\n";
    prompt << "最後に総合評価を記載してください。\n\n";
    prompt << "必ず日本語で回答してください。\n";
    prompt << "【出力フォーマット（厳密に守ること）】\n";
    prompt << "【概要】{全体の要約（短い日本語文）}\n\n";
    prompt << "- レビューの焦点:{短く日本語で記述} (危険度：{HIGH/MEDIUM/LOW})\n\n";
    prompt << "【総評】{全体の総合評価（短い日本語文）}\n\n";
    prompt << "Git diff:\n";
    prompt << diff;
    return prompt.str();
}

// InitOlllama:
// Ollama API の初期化（必要に応じてモデルのロードや設定を行う）
void InitOllama() {

	// ollama ps コマンドでモデルが起動しているか確認する
	std::string psOutput = exec("ollama ps");
    if (psOutput.find(MODEL_NAME) == std::string::npos) {
        // codellama が起動してない場合はモデルをserveで起動する
        std::cout << "Starting "<< MODEL_NAME << " model...\n";
        exec(std::string("ollama serve ") + MODEL_NAME + " &");
        // 起動後、少し待ってから再度 ps を確認する
        bool modelStarted = false;
        for (int i = 0; i < 5; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            psOutput = exec("ollama ps");
            if (psOutput.find(MODEL_NAME) != std::string::npos) {
                modelStarted = true;
                break;

            }
        }
    }

}

// JSON 文字列を安全にエスケープするユーティリティ
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// callOllama: 設定の endpoint/port/model を使うように変更
std::string callOllama(const std::string& prompt, const Config& cfg) {
    std::string escaped; // JSON エスケープは既存の json_escape を使; 再定義は避けるため簡単に reuse
    // 単純再利用: main.cpp 内の json_escape を使っている前提（関数は下に残す）
    escaped = json_escape(prompt);
    std::string payload =
        std::string("{\"model\":\"") + cfg.model +
        std::string("\",\"prompt\":\"") + escaped +
        std::string("\",\"stream\":false,\"temperature\":0.4,\"repeat_penalty\":1.2}");

    PayloadFile payloadFile{ csDataDir + "/payload.json" };
#ifdef _DEBUG
    payloadFile.keep_file();
#endif
    payloadFile.write_all(payload);

    // URL を構築（ポートが 0 のような不正値の検証は事前に行ってください）
    std::string url = cfg.endpoint;
    // endpoint にポートが入っていない場合は付与
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += ":" + std::to_string(cfg.port) + "/api/generate";

    std::string cmd =
        "curl -s -X POST " + url +
        " -H \"Content-Type: application/json\" -d @" + payloadFile.path;

    std::cout << "Executing command:\n" << cmd << "\n";
    std::string response = exec(cmd);
    return response;
}

// printHeader:
// プログラム開始時に表示するヘッダを出力する（視認性向上用）
void printHeader() {
    std::cout << "=============================\n";
    std::cout << " AI C++ Code Review\n";
    std::cout << "=============================\n\n";
}

int main() {
    if (!ensure_directory_exists(csDataDir)) {
        std::cerr << "Failed to create data directory: " << csDataDir << "\n";
        return 1;
    }

    // 設定読み込み（無ければデフォルトを書き出す）
    Config cfg = Config::load_or_create(csDataDir + "/config.json");

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::string timestamp = std::to_string(now_c);

    try {
        printHeader();
        InitOllama();

        std::cout << "Collecting git diff...\n";
        std::string diff = getGitDiff();
        if (diff.empty()) {
            std::cout << "No staged changes.\n";
            return 0;
        }

        std::cout << "Building prompt...\n";
        std::string prompt = buildPrompt(diff, cfg.review_focus);

        std::cout << "Sending to Ollama...\n";
        std::string response = callOllama(prompt, cfg);

#ifdef _DEBUG
        std::ofstream debugOut(csDataDir + "/debug_response_" + timestamp + ".txt", std::ios::binary);
        debugOut << response;
        debugOut.close();
#endif

        std::string sResultFile = csDataDir + "/review_result.txt";
        try {
            auto j = json::parse(response);
            if (j.contains("response") && j["response"].is_string()) {
                std::string parsed = j["response"].get<std::string>();
                std::cout << parsed << std::endl;
                std::ofstream out(sResultFile, std::ios::binary);
                if (!out) throw std::runtime_error("failed to open payload file: " + sResultFile);
                out << parsed;
                out.close();
            } else {
                std::cout << response << std::endl;
                std::ofstream out(sResultFile, std::ios::binary);
                if (!out) throw std::runtime_error("failed to open payload file: " + sResultFile);
                out << response;
                out.close();
            }
            if (j.contains("done_reason") && j["done_reason"].is_string()) {
                std::string done = j["done_reason"].get<std::string>();
                if (done != "stop") {
                    std::cerr << "Warning: done_reason = " << done << " (response may be truncated or stopped for another reason)\n";
                }
            }
        } catch (const json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            std::cout << response << std::endl;
            std::ofstream out(sResultFile, std::ios::binary);
            if (!out) throw std::runtime_error("failed to open payload file: " + sResultFile);
            out << response;
            out.close();
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    // 実行時間を出力
	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed = end - now;
    std::cout << "Execution time: " << elapsed.count() << " seconds\n";
    return 0;
}