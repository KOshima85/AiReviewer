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
   - 一時ファイル prompt.txt にプロンプトを書き出す
   - curl コマンドを組み立ててローカルの Ollama API に渡す（jq を使って JSON エスケープ）
   - exec で curl を実行して結果を取得して返す
   - 注意: 実運用ではシェル注入やファイル入出力の安全性に注意する（ここでは簡潔化）

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
 - C++14 準拠でコンパイル可能なコードを維持します。
*/

#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <array>
#include <memory>
#include <nlohmann/json.hpp>

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
#endif


//#define MODEL_NAME "codellama"
#define MODEL_NAME "qwen3.5:9b"

// exec:
// 与えられたシェルコマンドを実行し、その標準出力を文字列として返す。
// - 内部で popen を使ってプロセスを開き、fgets で出力を読み取る。
// - popen に失敗した場合は std::runtime_error を投げる。
// - 固定長バッファを使い、結果を result に追加して返す。
// 注意: 大きな出力やバイナリデータの扱いには注意が必要。
std::string exec(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;

    // popen の戻りを unique_ptr で管理し、pclose をデリータとして渡す
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);

    if (!pipe) {
        // popen が null を返したら例外を投げる（呼び出し元で捕捉する）
        throw std::runtime_error("popen() failed");
    }

    // fgets が nullptr を返すまで読み続ける（EOF まで）
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    return result;
}

// getGitDiff:
// ステージされた変更（--staged）の git diff を取得するラッパー
std::string getGitDiff() {
    return exec("git diff --staged");
}

// buildPrompt:
// AI に送るプロンプトを組み立てる。
// - レビューの焦点（メモリ安全性、未定義動作、例外安全性、性能、可読性）を明示する。
// - 各問題に対して重大度（HIGH/MEDIUM/LOW）を返すよう指示する。
// - git diff を末尾に含める。
std::string buildPrompt(const std::string& diff) {
    std::stringstream prompt;

    prompt << "You are a senior C++ engineer.\n";
    prompt << "Review the following git diff.\n";
    prompt << "Focus on:\n";
    prompt << "- memory safety\n";
    prompt << "- undefined behavior\n";
    prompt << "- exception safety\n";
    prompt << "- performance\n";
    prompt << "- readability\n\n";

    prompt << "Return issues with severity (HIGH/MEDIUM/LOW).\n\n";

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
        std::cout << "Starting codellama model...\n";
        exec("ollama serve codellama &");

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
        std::string("\",\"stream\":false}");

    // ファイルに書き出す（-d @file を使うことでシェルのクォート問題を回避）
    std::ofstream out("payload.json", std::ios::binary);
    out << payload;
    out.close();

    std::string cmd =
        "curl -s -X POST http://localhost:11434/api/generate "
        "-H \"Content-Type: application/json\" -d @payload.json";

    std::cout << "Executing command:\n" << cmd << "\n";
    return exec(cmd);
}

// printHeader:
// プログラム開始時に表示するヘッダを出力する（視認性向上用）
void printHeader() {
    std::cout << "=============================\n";
    std::cout << " AI C++ Code Review\n";
    std::cout << "=============================\n\n";
}

int main() {
    try {
        printHeader();

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
        std::string response = callOllama(prompt);

        try {
            auto j = json::parse(response);
            if (j.contains("response") && j["response"].is_string()) {
                std::string parsed = j["response"].get<std::string>();
                std::cout << parsed << std::endl;
                std::ofstream out("review_result.txt", std::ios::binary);
                out << parsed;
                out.close();
            } else {
                // "response" フィールドが無い・文字列でない場合は生の応答を使う
                std::cout << response << std::endl;
                std::ofstream out("review_result.txt", std::ios::binary);
                out << response;
                out.close();
            }
        } catch (const json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            std::cout << response << std::endl;
            std::ofstream out("review_result.txt", std::ios::binary);
            out << response;
            out.close();
        }

		// 終了前に一時ファイルを削除する（必要に応じて）
		std::remove("payload.json");


    }
    catch (const std::exception& e) {
        // 例外発生時はエラーメッセージを出力して終了
        std::cerr << "Error: " << e.what() << std::endl;
    }
}