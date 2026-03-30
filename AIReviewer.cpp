#include "AIReviewer.h"
#include "Exec.h"         // getGitDiff 用の exec 等を利用
#include "PayloadFile.h"  // 既存のまま一時ファイルを使う場合
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

using nlohmann::json;

AIReviewer::AIReviewer(const Config* cfg, LLMConnector& connector) noexcept
	:m_connector(connector), m_focus(cfg->review_focus), m_useStagedDiff(cfg->use_staged_diff)
{
}

void AIReviewer::Initialize() {
    m_connector.Initilize();
}

std::string AIReviewer::collectDiff() const {
    if (m_useStagedDiff) {
        return exec("git diff --staged -w");
    }
    return exec("git diff -w");
}

std::string AIReviewer::buildPrompt(const std::string& diff) const {
    std::stringstream prompt;
    prompt << "あなたは上級のC++エンジニアです。\nレビューの焦点:\n";
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
    std::filesystem::path out = std::filesystem::path(".aireviewr") / "review_result.txt";
    std::ofstream ofs(out, std::ios::binary);
    if (!ofs) throw std::runtime_error("failed to write result");
    // ここは応答のパース/検証を行ってから保存しても良い
    ofs << response;
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