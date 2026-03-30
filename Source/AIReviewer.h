#pragma once
#include <string>
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
    void AnalyzeResponse(const std::string response);

private:
    std::string collectDiff() const;
    std::string buildPrompt(const std::string& diff) const;
    std::string callModel(const std::string& prompt);
    void persistResult(const std::string& response) const;

    void replaceAll(std::string& str, const std::string& from, const std::string& to);

    LLMConnector& m_connector;
    std::vector<std::string> m_focus;
	bool m_useStagedDiff;
};