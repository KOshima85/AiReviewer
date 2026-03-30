#pragma once
/*
* LLMConnector: LLM（大規模言語モデル）との接続を管理する抽象クラス
* - LLMとの通信、プロンプトの送信、レスポンスの受信などを担当
* - 具体的なLLMの種類（例: OpenAI, Azure, Hugging Faceなど）に応じて派生クラスを実装する
*/

#include <string>
#include "Define.h"
#include "Config.h"

class LLMConnector
{
public:
	virtual ~LLMConnector() = default;
	virtual void Initilize() = 0;
	virtual std::string Call(const std::string& prompt) = 0;
	LLMConnector(const Config* cfg);
protected:
	static std::string json_escape(const std::string& s);

	const Config* cfg;

};

