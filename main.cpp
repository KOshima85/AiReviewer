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
/*
 Windows 環境では標準ライブラリの popen/pclose が
 _popen/_pclose という名前になっているためマクロで置き換える
*/
#define popen _popen
#define pclose _pclose
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

//#define MODEL_NAME "codellama"
//#define MODEL_NAME "qwen3.5:9b"
//#define MODEL_NAME "qwen3.5:4b"
#define MODEL_NAME "gemma3:4b"

// ディレクトリ名（変更可）
static const std::string csDataDir = ".aireviewr";

// C++ 標準の filesystem を条件付きで使う（C++17 の <filesystem> または experimental）
#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "No filesystem support: require <filesystem> or <experimental/filesystem>"
#endif

// ディレクトリ作成（存在すれば OK）
static bool ensure_directory_exists(const std::string& dir) {
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        return fs::is_directory(dir, ec);
    }
    // 親ディレクトリも含めて作成
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
std::string buildPrompt(const std::string& diff) {
    std::stringstream prompt;

    // システム的な指示を日本語で明確に与える
    prompt << "あなたは上級のC++エンジニアです。\n";
    prompt << "以下の git diff をレビューしてください。\n";
    prompt << "レビューの焦点:\n";
    prompt << "- メモリ安全性\n";
    prompt << "- 未定義動作\n";
    prompt << "- 例外安全性\n";
    prompt << "- 性能\n";
    prompt << "- 可読性\n\n";

    prompt << "各問題について重大度を (HIGH/MEDIUM/LOW) で示してください。\n\n";
    prompt << "問題は箇条書きしてください。\n\n";
    prompt << "説明は不要です。\n\n";

    // ここで日本語での出力を明示する
    prompt << "必ず日本語で回答してください。\n";

    prompt << "【出力フォーマット（厳密に守ること）】\n";
    prompt << "【概要】{全体の要約（短い日本語文）}";
    prompt << "- {レビューの焦点}({HIGH/MEDIUM/LOW}):{短く日本語で記述}\n\n";

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

// callOllama: jq を使わず C++ 側でエスケープして payload.json を作成する
std::string callOllama(const std::string& prompt) {
    // JSON エスケープしてペイロードを構築
    std::string escaped = json_escape(prompt);
    std::string payload =
        std::string("{\"model\":\"") + MODEL_NAME +
        std::string("\",\"prompt\":\"") + escaped +
		std::string("\",\"stream\":false,\"temperature\":0.4,\"repeat_penalty\":1.2}"); // temperature:発散の抑制、repeat_penalty:同じ内容の繰り返し抑制

    // payload.json を .aireviewr に書き出す
    std::string payload_path = csDataDir + "/payload.json";
    std::ofstream out(payload_path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open payload file: " + payload_path);
    out << payload;
    out.close();

    std::string cmd =
        "curl -s -X POST http://localhost:11434/api/generate "
        "-H \"Content-Type: application/json\" -d @" + payload_path;

    std::cout << "Executing command:\n" << cmd << "\n";
	std::string response = exec(cmd);
    // 終了前に一時ファイルを削除する（必要に応じて）
    std::remove((csDataDir + "/payload.json").c_str());
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

    // ディレクトリを確保
    if (!ensure_directory_exists(csDataDir)) {
        std::cerr << "Failed to create data directory: " << csDataDir << "\n";
        return 1;
    }

	// 実行日時を取得してファイル名に使う
	auto now = std::chrono::system_clock::now();
	std::time_t now_c = std::chrono::system_clock::to_time_t(now);
	std::string timestamp = std::to_string(now_c);


    try {
        printHeader();

        // TODO: Ollama以外のLLMに対応できるようにする
        InitOllama();

        std::cout << "Collecting git diff...\n";
        // ステージされた差分を取得
        std::string diff = getGitDiff();

        // 差分が空であれば処理を終了
        if (diff.empty()) {
            std::cout << "No staged changes.\n";
            return 0;
        }

        std::cout << "Building prompt...\n";
        // プロンプトを構築
        std::string prompt = buildPrompt(diff);

        std::cout << "Sending to Ollama...\n";
        // API に送信して応答を取得
        std::string response = callOllama(prompt);        // TODO: Ollama以外のLLMに対応できるようにする

#ifdef _DEBUG 
		// response を .aireviewr 以下に保存する（デバッグ用）
        // 終了後も確認できるように削除はしない
        std::ofstream debugOut(csDataDir + "/debug_response_" + timestamp + ".txt", std::ios::binary);
        debugOut << response;
		debugOut.close();
#endif

		std::string sResultFile = csDataDir + "/review_result.txt"; // 結果ファイル名。常に最新のみを保持する。
        try {
            auto j = json::parse(response);
            if (j.contains("response") && j["response"].is_string()) {
                std::string parsed = j["response"].get<std::string>();
                std::cout << parsed << std::endl;
                // 結果ファイルを .aireviewr 以下に出力
                std::ofstream out(sResultFile, std::ios::binary);
                if (!out) throw std::runtime_error("failed to open payload file: " + sResultFile);
                out << parsed;
                out.close();
            } else {
                // "response" フィールドが無い・文字列でない場合は生の応答を使う
                std::cout << response << std::endl;
                std::ofstream out(sResultFile, std::ios::binary);
                if (!out) throw std::runtime_error("failed to open payload file: " + sResultFile);
                out << response;
                out.close();
            }
		    // done_reason をチェックして警告を出す（json 解析後）
		    if (j.contains("done_reason") && j["done_reason"].is_string()) {
			    std::string done = j["done_reason"].get<std::string>();
			    if (done != "stop") {
				    std::cerr << "Warning: done_reason = " << done << " (response may be truncated or stopped for another reason)\n";
				    // 必要ならここで再試行やログ出力を行う
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



    }
    catch (const std::exception& e) {
        // 例外発生時はエラーメッセージを出力して終了
        std::cerr << "Error: " << e.what() << std::endl;
    }

    // 実行時間を出力
	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed = end - now;
    std::cout << "Execution time: " << elapsed.count() << " seconds\n";
	return 0;
}