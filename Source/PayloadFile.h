#pragma once
#include <string>
#include <fstream>
#include <system_error> // std::error_code
#include <filesystem>
#include <cerrno>

// PayloadFile: ペイロード用一時ファイルの RAII 管理
// - ファイル作成・書き込み失敗時に std::runtime_error を投げる
// - デストラクタでファイルを削除（削除失敗は無視）する
// - 成功時にファイルを残したければ KeepFile() を呼ぶ
struct PayloadFile {
    std::string path;
    std::ofstream ofs;
    bool keep;

    explicit PayloadFile(const std::string& p)
        : path(p), keep(false)
    {
        ofs.open(path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            int e = errno;
            throw std::runtime_error(std::string("PayloadFile: failed to create file '") + path + "': " + errnoMessage(e));
        }
    }

    // 書き込み（失敗時に例外）
    void WriteAll(const std::string& data) {
        ofs << data;
        if (!ofs) {
            int e = errno;
            throw std::runtime_error(std::string("PayloadFile: failed to write to '") + path + "': " + errnoMessage(e));
        }

        // flush の失敗は明示的に検出して例外を投げる
        ofs.flush();
        if (!ofs) {
            int e = errno;
            throw std::runtime_error(std::string("PayloadFile: failed to flush '") + path + "': " + errnoMessage(e));
        }
    }

    // 一時ファイルを残す（デバッグ等で使う）
    void KeepFile() noexcept { keep = true; }

    ~PayloadFile() {
        // デストラクタでは例外を外に出さない（リソース解放時の失敗は無視する）
        try {
            if (ofs.is_open()) {
                // close() が例外を投げる可能性がある実装もあるため捕捉する
                ofs.close();
            }
        } catch (...) {}

        if (!keep) {
            // 存在チェックとノン例外版 remove を使用して削除失敗を無視する
            std::error_code ec;
            if (std::filesystem::exists(path, ec) && !ec) {
                std::filesystem::remove(path, ec); // エラーはここで無視
            }
        }
    }

    // コピー禁止
    PayloadFile(const PayloadFile&) = delete;
    PayloadFile& operator=(const PayloadFile&) = delete;

private:
    // errno から移植性のあるメッセージを取得するヘルパ
    static inline std::string errnoMessage(int e) noexcept {
        try {
            std::error_code ec(e, std::generic_category());
            auto msg = ec.message();
            return msg.empty() ? "unknown error" : msg;
        } catch (...) {
            return "unknown error";
        }
    }
};