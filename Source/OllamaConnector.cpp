#include "OllamaConnector.h"
#include <iostream>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

#include "Exec.h"
#include "PayloadFile.h"

void OllamaConnector::Initilize()
{
    if(cfg == nullptr) {
        throw std::runtime_error("Config is null");
	}

    SetModelName(cfg->model.c_str());

    {
	    // ollama list コマンドでモデルが存在するか確認する
        std::string psOutput = exec("ollama list");
        if (psOutput.find(m_sModelName) == std::string::npos) {
		    // モデルが存在しない場合はpullしてくる
            std::cout << "Pulling " << m_sModelName << " model...\n";
		    exec(std::string("ollama pull ") + m_sModelName, false); // モデルDLは時間がかかるためlimitを無視させる
        }
    }
}

std::string OllamaConnector::Call(const std::string& prompt)
{
    if (cfg == nullptr) {
        throw std::runtime_error("Config is null");
    }

    std::string escaped; // JSON エスケープは既存の json_escape を使; 再定義は避けるため簡単に reuse
    // 単純再利用: main.cpp 内の json_escape を使っている前提（関数は下に残す）
    escaped = json_escape(prompt);
    std::string payload =
        std::string("{\"model\":\"") + cfg->model +
        std::string("\",\"prompt\":\"") + escaped +
        std::string("\",\"stream\":false,\"temperature\":0.4,\"repeat_penalty\":1.2}");

    PayloadFile payloadFile{ csDataDir + "/payload.json" };
#ifdef _DEBUG
    payloadFile.keep_file();
#endif
    payloadFile.write_all(payload);

    // URL を構築（ポートが 0 のような不正値の検証は事前に行ってください）
    std::string url = cfg->endpoint;
    // endpoint にポートが入っていない場合は付与
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += ":" + std::to_string(cfg->port) + "/api/generate";

    std::string cmd =
        "curl -s -X POST " + url +
        " -H \"Content-Type: application/json\" -d @" + payloadFile.path;

    std::cout << "Executing command:\n" << cmd << "\n";
    std::string response = exec(cmd);
    return response;
}

OllamaConnector::OllamaConnector(const Config* cfg):
	LLMConnector(cfg),
    m_sModelName("")
{
}

// 入力モデル名のサニタイズ処理を別関数に切り出し
std::string OllamaConnector::SanitizeModelName(std::string_view modelName)
{
    // 空入力はデフォルトへ
    std::string src = modelName.empty() ? std::string(MODEL_NAME) : std::string(modelName);

    // トリム（先頭/末尾の空白を除去）
    const std::string whitespace = " \t\r\n";
    auto first = src.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        src.clear();
    } else {
        auto last = src.find_last_not_of(whitespace);
        src = src.substr(first, last - first + 1);
    }

    if (src.empty()) {
        src = MODEL_NAME;
    }

    // 許可する文字: 英数字と一部記号
    const std::string allowedPunct = ":-_./+@";
    std::string cleaned;
    cleaned.reserve(src.size());
    for (unsigned char uc : src) {
        if (std::isalnum(uc) || allowedPunct.find(static_cast<char>(uc)) != std::string::npos) {
            cleaned.push_back(static_cast<char>(uc));
        } else {
            // 許可されない文字は '_' に置換（安全性のため）
            cleaned.push_back('_');
        }
    }

    // 連続する '_' を一つにまとめる（正規化）
    std::string normalized;
    normalized.reserve(cleaned.size());
    char prev = '\0';
    for (char c : cleaned) {
        if (c == '_' && prev == '_') continue;
        normalized.push_back(c);
        prev = c;
    }

    // 先頭/末尾の '_' を取り除く（任意の追加正規化）
    if (!normalized.empty() && normalized.front() == '_') {
        normalized.erase(normalized.begin());
    }
    if (!normalized.empty() && normalized.back() == '_') {
        normalized.pop_back();
    }

    // 空になった場合はデフォルトを使用
    if (normalized.empty()) normalized = MODEL_NAME;

    return normalized;
}

void OllamaConnector::SetModelName(const char* modelName)
{
    std::string normalized = SanitizeModelName(modelName ? modelName : "");
    std::size_t maxlen = sizeof(m_sModelName) - 1;
    std::size_t copylen = std::min(maxlen, normalized.size());
    if (copylen > 0) {
        std::memcpy(m_sModelName, normalized.data(), copylen);
    }
    m_sModelName[copylen] = '\0';
}
