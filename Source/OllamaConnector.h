#pragma once
#include "LLMConnector.h"

class OllamaConnector : public LLMConnector
{
public:
	virtual void Initilize() override;
	virtual std::string Call(const std::string& prompt) override;

	void SetModelName(char modelName) { m_sModelName[0] = modelName; m_sModelName[1] = '\0'; }	
	OllamaConnector(const Config* cfg);

private:
	char m_sModelName[256];
	void SetModelName(const char* modelName);
	// 文字列のサニタイズ関数
	//char sanitizeModelName(const char* modelName);
	std::string SanitizeModelName(std::string_view modelName);

};
