# AI C++ Code Review (AiReviewer)

## 概要
- ステージされた git 差分を取得し、ローカルで稼働する LLM（デフォルトは Ollama）へ送って自動コードレビューを行うツールです。
- レビュー結果はコンソールに出力されます。また`.aireviewr/review_result.txt` にも保存されます。
- ローカルLLMを利用することで完全にローカルで完結します。

## 主な特徴
- 設定ファイル (`.aireviewr/config.json`) でエンドポイント・ポート・使用モデル・レビュー焦点・**危険度ブロック閾値**を変更可能

## 必須要件

- [Ollama](https://www.ollama.com/) ローカルLLM。公式サイトからインストールしてください。
- `C++20` 対応コンパイラ（MSVC / g++ / clang++）
- `curl`（現在は外部コマンドで HTTP リクエストを発行）
- `git`（差分取得のため）
- nlohmann/json（json周りの処理で利用。ヘッダオンリー）

## 設定ファイル

- パス: `.aireviewr/config.json`
- サンプル:

```json
{
  "endpoint": "http://localhost",
  "port": 11434,
  "model": "gemma3:4b",
  "review_focus": ["メモリ安全性","未定義動作","例外安全性","性能","可読性","SOLID原則"],
  "use_staged_diff": true,
  "max_high": 0,
  "max_medium": -1,
  "max_low": -1,
  "include_patterns": ["*.cpp", "*.h"],
  "exclude_patterns": ["*.generated.cpp"]
}
```

| フィールド | 型 | デフォルト | 説明 |
|-----------|---|-----------|------|
| `endpoint` | string | `"http://localhost"` | Ollama エンドポイント（`http://` または `https://` で始まる必要あり） |
| `port` | int | `11434` | Ollama のポート番号 |
| `model` | string | `"gemma3:12b"` | 使用するモデル名 |
| `review_focus` | string[] | 上記6項目 | レビューで重視する観点 |
| `use_staged_diff` | bool | `true` | `true`: ステージ済み差分、`false`: ワーキングツリー差分 |
| `max_high` | int | `0` | 許容する HIGH 件数の上限（超過でコミットブロック、`-1` で無制限） |
| `max_medium` | int | `-1` | 許容する MEDIUM 件数の上限（`-1` で無制限） |
| `max_low` | int | `-1` | 許容する LOW 件数の上限（`-1` で無制限） |
| `include_patterns` | string[] | `[]` | レビュー対象ファイルの glob パターン（空=全ファイル対象）例: `["*.cpp", "*.h"]` |
| `exclude_patterns` | string[] | `[]` | レビューから除外するファイルの glob パターン 例: `["*.generated.cpp"]` |

> **コミットブロック**: `max_high=0`（デフォルト）の場合、HIGH が 1 件以上あると終了コード 1 を返します。
> git pre-commit フックで利用すると、危険度の高い問題があるコミットを自動的にブロックできます。

> **ファイルフィルタリング**: `include_patterns` と `exclude_patterns` は git の pathspec として渡されます。
> 両方空の場合はすべてのファイルがレビュー対象になります。

## ビルド手順（Windows / Visual Studio）

1. ソリューションを Visual Studio で開く
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
    - gemma3:12b RTX 3060(VRAM12GB) で応答時間が20～30秒(使用GPUメモリ10.3GB)ほどになります。
    - gemma3:4b 5～10秒(使用GPUメモリ5.2GB)ほどで応答が得られます。
    - Thinking 対応のモデルは返答の品質が向上しますが、応答時間も長くなります。

- tools/AIReview.bat
    - 実行用のbatファイルです。実行したいプロジェクトルートにコピーして使用してください。
    - 環境に合わせてパスを変更してください。
