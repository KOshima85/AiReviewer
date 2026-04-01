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
#include <algorithm>

using nlohmann::json;

AIReviewer::AIReviewer(const Config* cfg, LLMConnector& connector) noexcept
	:m_connector(connector), m_focus(cfg->review_focus), m_useStagedDiff(cfg->use_staged_diff),
	 m_maxHigh(cfg->max_high), m_maxMedium(cfg->max_medium), m_maxLow(cfg->max_low),
	 m_includePatterns(cfg->include_patterns), m_excludePatterns(cfg->exclude_patterns)
{
}

void AIReviewer::Initialize() {
    m_connector.Initialize();
}

std::string AIReviewer::collectDiff() const {
    std::string cmd = "git diff";
    if (m_useStagedDiff) cmd += " --staged";
    cmd += " -w";

    // include/exclude パターンが指定されている場合は git pathspec として追加する
    // 安全でないパターンは無視し、有効なもののみ渡す
    bool hasFilter = false;
    for (const auto& p : m_includePatterns) {
        if (isValidGlobPattern(p)) hasFilter = true;
    }
    for (const auto& p : m_excludePatterns) {
        if (isValidGlobPattern(p)) hasFilter = true;
    }

    if (hasFilter) {
        cmd += " --";
        for (const auto& p : m_includePatterns) {
            if (isValidGlobPattern(p)) cmd += " \"" + p + "\"";
        }
        for (const auto& p : m_excludePatterns) {
            if (isValidGlobPattern(p)) cmd += " \":(exclude)" + p + "\"";
        }
    }

    return exec(cmd);
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

int AIReviewer::AnalyzeResponse(const std::string& response)
{
    int exitCode = 0;
    try {
        auto j = json::parse(response);
        if (j.contains("response") && j["response"].is_string()) {
			// "response" フィールドがある場合はそこからレビュー結果を取り出す
            std::string parsed = j["response"].get<std::string>();

			// 結果をファイルに保存する
			persistResult(parsed);

            // カラー適用前に危険度をカウントする
            int highCount   = countOccurrences(parsed, "HIGH");
            int mediumCount = countOccurrences(parsed, "MEDIUM");
            int lowCount    = countOccurrences(parsed, "LOW");

			// 危険度ラベルに ANSI カラーを適用する
            parsed = applySeverityColors(parsed);

            std::cout << parsed << std::endl;

            // 閾値チェック（-1 は無制限）
            auto exceeds = [](int count, int max) {
                return max >= 0 && count > max;
            };
            if (exceeds(highCount, m_maxHigh) ||
                exceeds(mediumCount, m_maxMedium) ||
                exceeds(lowCount, m_maxLow)) {
                std::cerr << "[BLOCK] 危険度が閾値を超えたためコミットをブロックします"
                          << " (HIGH=" << highCount
                          << ", MEDIUM=" << mediumCount
                          << ", LOW=" << lowCount << ")\n";
                exitCode = 1;
            }
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
    return exitCode;
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

std::vector<std::pair<std::string, std::string>> AIReviewer::collectCommits(int n) const {
    std::string cmd = "git log -" + std::to_string(n) + " --format=\"%H %s\"";
    std::string output = exec(cmd);

    std::vector<std::pair<std::string, std::string>> commits;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        // 末尾の \r を除去（Windows 環境対策）
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 41) continue; // SHA(40文字) + スペース 未満は無視
        std::string sha = line.substr(0, 40);
        std::string subject = line.size() > 41 ? line.substr(41) : "";
        commits.emplace_back(sha, subject);
    }
    return commits;
}

std::string AIReviewer::getCommitDiff(const std::string& sha) const {
    if (!isValidCommitSha(sha)) {
        throw std::invalid_argument("Invalid commit SHA: " + sha);
    }
    std::string cmd = "git show -p --no-color " + sha;

    // include/exclude パターンが指定されている場合は git pathspec として追加する
    bool hasFilter = false;
    for (const auto& p : m_includePatterns) {
        if (isValidGlobPattern(p)) hasFilter = true;
    }
    for (const auto& p : m_excludePatterns) {
        if (isValidGlobPattern(p)) hasFilter = true;
    }
    if (hasFilter) {
        cmd += " --";
        for (const auto& p : m_includePatterns) {
            if (isValidGlobPattern(p)) cmd += " \"" + p + "\"";
        }
        for (const auto& p : m_excludePatterns) {
            if (isValidGlobPattern(p)) cmd += " \":(exclude)" + p + "\"";
        }
    }

    return exec(cmd);
}

void AIReviewer::persistHistoryResult(const std::string& content) const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::string timestamp = std::to_string(now_c);
    std::filesystem::path out = std::filesystem::path(csDataDir) / ("history_review_" + timestamp + ".txt");
    std::ofstream ofs(out, std::ios::binary);
    if (!ofs) throw std::runtime_error("failed to write history result");
    ofs << content;
}

int AIReviewer::RunCommit(const std::string& sha) {
    if (!isValidCommitSha(sha)) {
        throw std::invalid_argument("Invalid commit SHA: " + sha);
    }

    std::cout << "Collecting diff for commit " << sha << "...\n";
    std::string diff = getCommitDiff(sha);
    if (diff.empty()) {
        std::cout << "No diff found for commit " << sha << ".\n";
        return 0;
    }

    std::cout << "Building prompt...\n";
    auto prompt = buildPrompt(diff);

    std::cout << "Sending to LLM...\n";
    auto resp = callModel(prompt);

#ifdef _DEBUG
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::string timestamp = std::to_string(now_c);
    std::ofstream debugOut(csDataDir + "/debug_response_" + timestamp + ".txt", std::ios::binary);
    debugOut << resp;
    debugOut.close();
#endif

    return AnalyzeResponse(resp);
}

int AIReviewer::RunHistory(int n) {
    auto commits = collectCommits(n);
    if (commits.empty()) {
        std::cout << "No commits found.\n";
        return 0;
    }

    int totalCommits = static_cast<int>(commits.size());
    std::cout << "Reviewing " << totalCommits << " commit(s)...\n\n";

    int maxExitCode = 0;
    std::ostringstream historyLog;

    for (int i = 0; i < totalCommits; ++i) {
        const auto& [sha, subject] = commits[i];
        std::string shortSha = sha.substr(0, 7);

        std::cout << "------------------------------------------------------------\n";
        std::cout << "[" << (i + 1) << "/" << totalCommits << "] "
                  << shortSha << " " << subject << "\n";
        std::cout << "------------------------------------------------------------\n";

        historyLog << "============================================================\n";
        historyLog << "[" << (i + 1) << "/" << totalCommits << "] "
                   << shortSha << " " << subject << "\n";
        historyLog << "============================================================\n";

        std::string diff = getCommitDiff(sha);
        if (diff.empty()) {
            std::cout << "  (no diff, skipping)\n\n";
            historyLog << "(no diff, skipped)\n\n";
            continue;
        }

        std::cout << "Building prompt...\n";
        auto prompt = buildPrompt(diff);

        std::cout << "Sending to LLM...\n";
        auto resp = callModel(prompt);

        // レスポンスをファイルに一時保存（既存の persistResult を流用）
        persistResult(resp);

        int code = AnalyzeResponse(resp);
        maxExitCode = std::max(maxExitCode, code);

        // 履歴ログにも追記（ANSI カラーなしのテキストとして保存）
        try {
            auto j = json::parse(resp);
            if (j.contains("response") && j["response"].is_string()) {
                historyLog << j["response"].get<std::string>() << "\n\n";
            } else {
                historyLog << resp << "\n\n";
            }
        } catch (...) {
            historyLog << resp << "\n\n";
        }

        std::cout << "\n";
    }

    std::cout << "------------------------------------------------------------\n";
    std::cout << "History review complete. (" << totalCommits << " commits)\n";

    persistHistoryResult(historyLog.str());

    return maxExitCode;
}