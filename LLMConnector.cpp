#include "LLMConnector.h"


LLMConnector::LLMConnector(const Config& cfg) :
    cfg(cfg)
{
}

// JSON 文字列を安全にエスケープするユーティリティ
std::string LLMConnector::json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else {
                out += c;
            }
        }
    }
    return out;
}