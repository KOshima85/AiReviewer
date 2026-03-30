# AI C++ Code Review (AiReviewer)

## 概要
- ステージされた git 差分を取得し、ローカルで稼働する LLM（デフォルトは Ollama）へ送って自動コードレビューを行うツール。
- レビュー結果はコンソールに出力され、`.aireviewr/review_result.txt` に保存されます。
- ローカルLLMを利用することで完全にローカルで完結します。

## 主な特徴
- 設定ファイル (`.aireviewr/config.json`) でエンドポイント・ポート・使用モデル・レビュー焦点を変更可能

## 必須要件

- `C++20` 対応コンパイラ（MSVC / g++ / clang++）
- `curl`（現在は外部コマンドで HTTP リクエストを発行）
- `git`（差分取得のため）
- ローカルの Ollama（デフォルト設定は http://localhost:11434）
- nlohmann/json（ヘッダオンリー）

## 設定ファイル

- パス: `.aireviewr/config.json`
- サンプル:

```json
{
  "endpoint": "http://localhost",
  "port": 11434, // Ollama のデフォルトポート.セキュリティ要件に合わせて変更・監視してください
  "model": "gemma3:4b", // 使用するモデル名
  "review_focus": ["メモリ安全性","未定義動作","例外安全性","性能","可読性","SOLID原則"],
  "use_staged_diff": true, // ステージされた差分を使用するか（true）、ワーキングツリーの差分を使用するか（false）
}
```

## ビルド手順（Windows / Visual Studio）

1. 1. ソリューションを Visual Studio で開く
2. nlohmann/json のインクルードパスを追加（必要な場合）
3. Build > Build Solution
4. 必要であれば`aireviewr.exe`のパスを環境変数に追加 

## 実行手順
1. git リポジトリのルートへ移動: `cd <path_to_repo>`
1. 変更をステージ： `git add <files>`
2. 実行ファイルを起動： `{path/to}/aireviewr`（Windows は実行ファイル名）
3. 出力： コンソール表示および `.aireviewr/review_result.txt` に結果が保存されます。デバッグビルド時は `.aireviewr/debug_response_<timestamp>.txt` が出力されることがあります。

トラブルシューティング
- Ollama が応答しない: `ollama ps` でモデル状況を確認し、必要なら `ollama serve <model>` を起動
- curl が無い: システムに curl をインストールするか、OllamaConnector を libcurl 等へ置換

## その他

- モデルについて: 
    - gemma3:12b RTX 3060(VRAM12GB) で応答時間が20～30秒ほどになります。
    - gemma3:4b 5～10秒ほどで応答が得られます。
    - Thinking 対応のモデルは返答の品質が向上しますが、応答時間も長くなります。
