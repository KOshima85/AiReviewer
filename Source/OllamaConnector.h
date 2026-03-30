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
	void setModelName(const char* modelName);
	std::string sanitizeModelName(std::string_view modelName);

};
