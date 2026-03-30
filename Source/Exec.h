#pragma once
#include <string>
#include <chrono>

// exec:
// 与えられたシェルコマンドを実行し、その標準出力を文字列として返す。
// - 内部で popen を使ってプロセスを開き、fread で出力を読み取る。
// - popen に失敗した場合は std::runtime_error を投げる。
// - 固定長バッファを使い、結果を result に追加して返す。
// - timeout: データが届かない状態が続いた際の最大待機時間（0 = 無制限）
// 注意: 大きな出力やバイナリデータの扱いには注意が必要。
std::string exec(const std::string& cmd, std::chrono::milliseconds timeout = std::chrono::milliseconds(0));