#pragma once
#include <string>
#include <utility>
#include <vector>
#include "Config.h"
#include "LLMConnector.h"

class AIReviewer {
public:
    AIReviewer(const Config* cfg, LLMConnector& connector) noexcept;
    // 初期化（必要ならモデル起動など）
    void Initialize();
    // 1 回分のレビューを実行して結果文字列を返す（例外は呼び出し元で処理）
    std::string RunOnce();
    // 直近 n コミットを順番にレビューする。全コミット中の最大 exitCode を返す
    int RunHistory(int n);
    // レスポンスを解析して表示する。危険度が閾値超の場合は 1 を返す（コミットブロック用）
    int AnalyzeResponse(const std::string& response);

private:
    std::string collectDiff() const;
    std::string buildPrompt(const std::string& diff) const;
    std::string callModel(const std::string& prompt);
    void persistResult(const std::string& response) const;
    // 直近 n 件のコミット一覧を { SHA, subject } のペアで返す
    std::vector<std::pair<std::string, std::string>> collectCommits(int n) const;
    // 指定コミットの差分を git show で取得する
    std::string getCommitDiff(const std::string& sha) const;
    // 複数コミットのレビュー結果をファイルに保存する
    void persistHistoryResult(const std::string& content) const;

    LLMConnector& m_connector;
    std::vector<std::string> m_focus;
	bool m_useStagedDiff;
    int m_maxHigh;
    int m_maxMedium;
    int m_maxLow;
    std::vector<std::string> m_includePatterns;
    std::vector<std::string> m_excludePatterns;
};