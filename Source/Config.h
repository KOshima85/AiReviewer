#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>


#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "No filesystem support: require <filesystem> or <experimental/filesystem>"
#endif

inline constexpr char MODEL_NAME[] = "gemma3:12b";

// ----- Config: 設定ファイルの読み書きと構造 -----
struct Config {
    std::string endpoint; // 例: "http://localhost"
    int port;             // 例: 11434
    std::string model;    // 例: "gemma3:4b"
    std::vector<std::string> review_focus; // レビューの焦点
	bool use_staged_diff; // git diff で --staged を使うか
    // 危険度ブロック閾値: -1=無制限, 0以上=その件数まで許容(超過でブロック)
    int max_high;
    int max_medium;
    int max_low;

	// デフォルト設定
    static Config Defaults() {
        return Config{
            "http://localhost",
            11434,
            MODEL_NAME,
            { "メモリ安全性", "未定義動作", "例外安全性", "性能", "可読性","SOLID原則"},
			true,
            0,   // max_high   : HIGH 0件まで許容 (1件でもブロック)
            -1,  // max_medium : 無制限
            -1   // max_low    : 無制限
        };
    }

    // 指定パスから読み込み。存在しなければデフォルトを書き出して返す。
    static Config LoadOrCreate(const std::string& path) {
        Config cfg = Defaults();
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            // ディレクトリは既に ensure_directory_exists で作られている想定
            // デフォルトを書き出す
            nlohmann::json j;
            j["endpoint"] = cfg.endpoint;
            j["port"] = cfg.port;
            j["model"] = cfg.model;
            j["review_focus"] = cfg.review_focus;
			j["use_staged_diff"] = cfg.use_staged_diff;
            j["max_high"]   = cfg.max_high;
            j["max_medium"] = cfg.max_medium;
            j["max_low"]    = cfg.max_low;
            std::ofstream out(path, std::ios::binary);
            if (out) out << j.dump(2);
            return cfg;
        }

        // ファイルがあれば読み込み、必要箇所を検証して採用する
        try {
            std::ifstream in(path, std::ios::binary);
            if (!in) return cfg;
            nlohmann::json j;
            in >> j;
            if (j.contains("endpoint") && j["endpoint"].is_string()) {
                std::string ep = j["endpoint"].get<std::string>();
                // http:// または https:// で始まる場合のみ採用し、不正値はデフォルトを維持
                if (ep.rfind("http://", 0) == 0 || ep.rfind("https://", 0) == 0) {
                    cfg.endpoint = ep;
                }
            }
            if (j.contains("port") && (j["port"].is_number_integer() || j["port"].is_string())) {
                if (j["port"].is_number_integer()) cfg.port = j["port"].get<int>();
                else {
                    try { cfg.port = std::stoi(j["port"].get<std::string>()); }
                    catch (...) {}
                }
            }
            if (j.contains("model") && j["model"].is_string()) cfg.model = j["model"].get<std::string>();
            if (j.contains("review_focus") && j["review_focus"].is_array()) {
                cfg.review_focus.clear();
                for (auto& it : j["review_focus"]) {
                    if (it.is_string()) cfg.review_focus.push_back(it.get<std::string>());
                }
                if (cfg.review_focus.empty()) cfg.review_focus = Defaults().review_focus;
            }
            if (j.contains("use_staged_diff") && j["use_staged_diff"].is_boolean()) {
                cfg.use_staged_diff = j["use_staged_diff"].get<bool>();
            }
            auto readIntOrDefault = [&](const char* key, int& out) {
                if (j.contains(key) && j[key].is_number_integer()) {
                    out = j[key].get<int>();
                }
            };
            readIntOrDefault("max_high",   cfg.max_high);
            readIntOrDefault("max_medium", cfg.max_medium);
            readIntOrDefault("max_low",    cfg.max_low);
        }
        catch (...) {
            // 読み込み/解析失敗はデフォルトを返す（既存ファイルは上書きしない）
        }
        return cfg;
    }
};
