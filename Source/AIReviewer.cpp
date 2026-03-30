#include "AIReviewer.h"
#include "Exec.h"         // getGitDiff 用の exec 等を利用
#include "PayloadFile.h"  // 既存のまま一時ファイルを使う場合
#include "StringUtils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iostream>

using nlohmann::json;

AIReviewer::AIReviewer(const Config* cfg, LLMConnector& connector) noexcept
	:m_connector(connector), m_focus(cfg->review_focus), m_useStagedDiff(cfg->use_staged_diff)
{
}

void AIReviewer::Initialize() {
    m_connector.Initialize();
}

std::string AIReviewer::collectDiff() const {
    if (m_useStagedDiff) {
        return exec("git diff --staged -w");
    }
    return exec("git diff -w");
}

std::string AIReviewer::buildPrompt(const std::string& diff) const {
    std::stringstream prompt;
    prompt << "あなたは上級のC++エンジニアです。\n";
    prompt << "以下の git diff をレビューしてください。\n";
    prompt << "レビューの焦点:\n";
    for (auto& f : m_focus) prompt << "- " << f << "\n";
    prompt << "\n各問題について危険度を (HIGH/MEDIUM/LOW) で示してください。\n\n";
    prompt << "\n危険度は HIGH=致命的, MEDIUM=普通, LOW=軽微 とします。\n\n";
    prompt << "問題は箇条書きしてください。\n\n";
    prompt << "最後に総合評価を記載してください。\n\n";
    prompt << "必ず日本語で回答してください。\n";
    prompt << "【出力フォーマット（厳密に守ること）】\n";
    prompt << "【概要】{全体の要約（短い日本語文）}\n\n";
    prompt << "- レビューの焦点:{短く日本語で記述} (危険度：{HIGH/MEDIUM/LOW})\n\n";
    prompt << "【総評】{全体の総合評価（短い日本語文）}\n\n";
    prompt << "\nGit diff:\n" << diff;
    return prompt.str();
}

std::string AIReviewer::callModel(const std::string& prompt) {
    // 既存の Connector を使う（Config を渡す）
    return m_connector.Call(prompt);
}

// persistResult:
// - レビュー結果をファイルに保存する
void AIReviewer::persistResult(const std::string& response) const {
    std::filesystem::path out = std::filesystem::path(csDataDir) / "review_result.txt";
    std::ofstream ofs(out, std::ios::binary);
    if (!ofs) throw std::runtime_error("failed to write result");
    // ここは応答のパース/検証を行ってから保存しても良い
    ofs << response;
}

void AIReviewer::AnalyzeResponse(const std::string& response)
{
    try {
        auto j = json::parse(response);
        if (j.contains("response") && j["response"].is_string()) {
			// "response" フィールドがある場合はそこからレビュー結果を取り出す
            std::string parsed = j["response"].get<std::string>();

			// 結果をファイルに保存する
			persistResult(parsed);

			// 危険度を色付けする（ターミナルで見やすくするための簡易的な方法）
            // TODO: 色分け処理を適切に分離する（別関数にする。必要であればhtmlタグにするなどできると良いかも）
            // TODO: 危険度をカウントして一定値以下ならコミットできるようにする等の機能も検討したい
            replaceAll(parsed, "HIGH", "\033[31mHIGH\033[0m");
            replaceAll(parsed, "MEDIUM", "\033[33mMEDIUM\033[0m");
            replaceAll(parsed, "LOW", "\033[32mLOW\033[0m");

            std::cout << parsed << std::endl;
        }
        else {
			// "response" フィールドがない場合は全体をそのまま出力する
            std::cout << response << std::endl;
            persistResult(response);
        }
        if (j.contains("done_reason") && j["done_reason"].is_string()) {
            std::string done = j["done_reason"].get<std::string>();
            if (done != "stop") {
                std::cerr << "Warning: done_reason = " << done << " (response may be truncated or stopped for another reason)\n";
            }
        }
    }
    catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        std::cout << response << std::endl;
        persistResult(response);
    }
}

std::string AIReviewer::RunOnce() {
    std::cout << "Collecting git diff...\n";
    auto diff = collectDiff();
    if (diff.empty()) 
    {
        std::cout << "No staged changes.\n";
        return std::string{};
    }
    std::cout << "Building prompt...\n";
    auto prompt = buildPrompt(diff);

    std::cout << "Sending to LLM...\n";
    auto resp = callModel(prompt);

    persistResult(resp);

#ifdef _DEBUG
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::string timestamp = std::to_string(now_c);
    std::ofstream debugOut(csDataDir + "/debug_response_" + timestamp + ".txt", std::ios::binary);
    debugOut << resp;
    debugOut.close();
#endif

    return resp;
}