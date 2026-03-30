#include "OllamaConnector.h"
#include <iostream>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
#include <atomic>
#include <cstdlib>

#include "Exec.h"
#include "PayloadFile.h"

#ifdef _WIN32
#include <process.h>
static int getCurrentPid() { return _getpid(); }
#else
#include <unistd.h>
static int getCurrentPid() { return static_cast<int>(getpid()); }
#endif

// プロセス内でユニークなペイロードファイル名を生成する
// 形式: payload_<PID>_<連番>.json
static std::string makePayloadPath() {
    static std::atomic<int> s_counter{0};
    int idx = s_counter.fetch_add(1, std::memory_order_relaxed);
    return std::string(csDataDir) + "/payload_" +
           std::to_string(getCurrentPid()) + "_" +
           std::to_string(idx) + ".json";
}

void OllamaConnector::Initialize()
{
    if(cfg == nullptr) {
        throw std::runtime_error("Config is null");
	}

    setModelName(cfg->model.c_str());

    {
	    // ollama list コマンドでモデルが存在するか確認する
        std::string psOutput = exec("ollama list");
        if (psOutput.find(m_sModelName) == std::string::npos) {
		    // モデルが存在しない場合はpullしてくる
            std::cout << "Pulling " << m_sModelName << " model...\n";
		    exec(std::string("ollama pull ") + m_sModelName); // モデルDLは時間がかかるためタイムアウトなし
        }
    }
}

std::string OllamaConnector::Call(const std::string& prompt)
{
    if (cfg == nullptr) {
        throw std::runtime_error("Config is null");
    }

    std::string escaped; // JSONエスケープは基底クラスの jsonEscape を使用
    // 単純再利用: 基底クラスの jsonEscape を使用
    escaped = jsonEscape(prompt);
    std::string payload =
        std::string("{\"model\":\"") + cfg->model +
        std::string("\",\"prompt\":\"") + escaped +
        std::string("\",\"stream\":false,\"temperature\":0.4,\"repeat_penalty\":1.2}");

    PayloadFile payloadFile{ makePayloadPath() };
#ifdef _DEBUG
    payloadFile.KeepFile();
#endif
    payloadFile.WriteAll(payload);

    // URL を構築（ポートが 0 のような不正値の検証は事前に行ってください）
    std::string url = cfg->endpoint;

    // endpoint が http:// または https:// で始まることを確認する
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        throw std::runtime_error(
            "Invalid endpoint URL: must start with 'http://' or 'https://'. Got: " + url);
    }

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
std::string OllamaConnector::sanitizeModelName(std::string_view modelName)
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

void OllamaConnector::setModelName(const char* modelName)
{
    std::string normalized = sanitizeModelName(modelName ? modelName : "");
    std::size_t maxlen = sizeof(m_sModelName) - 1;
    std::size_t copylen = std::min(maxlen, normalized.size());
    if (copylen > 0) {
        std::memcpy(m_sModelName, normalized.data(), copylen);
    }
    m_sModelName[copylen] = '\0';
}
