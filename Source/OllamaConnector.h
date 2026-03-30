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
    // 修正後
    void SetModelName(const char* modelName) { strncpy_s(m_sModelName, sizeof(m_sModelName), modelName, _TRUNCATE); }
};
