#include "OllamaConnector.h"
#include <iostream>
#include <thread>

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





