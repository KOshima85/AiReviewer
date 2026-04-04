#pragma once
#include "LLMConnector.h"

class OllamaConnector : public LLMConnector
{
public:
	virtual void Initialize() override;
	virtual std::string Call(const std::string& prompt) override;

	OllamaConnector(const Config* cfg);

private:
	std::string m_sModelName;
	std::string sanitizeModelName(std::string_view modelName);

};
