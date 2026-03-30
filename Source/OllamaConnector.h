#pragma once
#include "LLMConnector.h"

class OllamaConnector : public LLMConnector
{
public:
	virtual void Initialize() override;
	virtual std::string Call(const std::string& prompt) override;

	OllamaConnector(const Config* cfg);

private:
	char m_sModelName[256];
	void SetModelName(const char* modelName);
	std::string SanitizeModelName(std::string_view modelName);

};
