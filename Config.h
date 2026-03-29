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

using nlohmann::json;

#define MODEL_NAME "gemma3:4b"

// ----- Config: 設定ファイルの読み書きと構造 -----
struct Config {
    std::string endpoint; // 例: "http://localhost"
    int port;             // 例: 11434
    std::string model;    // 例: "gemma3:4b"
    std::vector<std::string> review_focus; // レビューの焦点

	// デフォルト設定
    static Config defaults() {
        return Config{
            "http://localhost",
            11434,
            MODEL_NAME,
            { "メモリ安全性", "未定義動作", "例外安全性", "性能", "可読性","SOLID原則"}
        };
    }

    // 指定パスから読み込み。存在しなければデフォルトを書き出して返す。
    static Config load_or_create(const std::string& path) {
        Config cfg = defaults();
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            // ディレクトリは既に ensure_directory_exists で作られている想定
            // デフォルトを書き出す
            json j;
            j["endpoint"] = cfg.endpoint;
            j["port"] = cfg.port;
            j["model"] = cfg.model;
            j["review_focus"] = cfg.review_focus;
            std::ofstream out(path, std::ios::binary);
            if (out) out << j.dump(2);
            return cfg;
        }

        // ファイルがあれば読み込み、必要箇所を検証して採用する
        try {
            std::ifstream in(path, std::ios::binary);
            if (!in) return cfg;
            json j;
            in >> j;
            if (j.contains("endpoint") && j["endpoint"].is_string()) cfg.endpoint = j["endpoint"].get<std::string>();
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
                if (cfg.review_focus.empty()) cfg.review_focus = defaults().review_focus;
            }
        }
        catch (...) {
            // 読み込み/解析失敗はデフォルトを返す（既存ファイルは上書きしない）
        }
        return cfg;
    }
};
