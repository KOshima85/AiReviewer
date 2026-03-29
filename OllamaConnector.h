#pragma once
#include "LLMConnector.h"

class OllamaConnector : public LLMConnector
{
public:
	virtual void Initilize() override;
	virtual std::string Call(const std::string& prompt, const Config& cfg) override;

	void SetModelName(char modelName) { m_sModelName[0] = modelName; m_sModelName[1] = '\0'; }	

private:
	char m_sModelName[256];
};
