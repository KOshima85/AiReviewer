# AI C++ Code Review

簡潔な説明
- このツールはステージされた git 差分（--staged）を収集し、ローカルで稼働する Ollama API に送信してコードレビューを行い、結果をファイルに保存します。
- 出力はコンソールに表示され、結果はリポジトリ直下のディレクトリ `.aireviewr` に保存されます。

主な特徴
- C++20 準拠でコンパイル可能
- ローカルの Ollama モデルを利用（デフォルトモデルは main.cpp の `MODEL_NAME` マクロで指定）
- 結果保存先: .aireviewr/review_result.txt（デフォルト、最新のみ）
- デバッグビルド時は `.aireviewr/debug_response_<timestamp>.txt` を生成

必要条件
- `C++20` に対応するコンパイラ（Visual Studio 2017 以降の MSVC、または `g++`/`clang++`）
- __curl__（プログラム内で curl コマンドを呼び出します）
- __git__（ステージされた差分を取得）
- Ollama（ローカル API が http://localhost:11434 で応答すること）
- nlohmann/json（ヘッダオンリーライブラリ、プロジェクトで提供またはインクルードパスに追加）

ビルド手順（Windows / Visual Studio）
1. Visual Studio でソリューション／プロジェクトを開く。
2. 必要なら nlohmann/json のインクルードパスをプロジェクトの __C/C++ > Additional Include Directories__ に追加する。
3. Build > Build Solution を実行してビルドする。

ビルド手順（Linux / macOS）
- 例（nlohmann/json が /usr/include にある場合）:
  g++ -std=c++14 -pthread -o aireviewr main.cpp

実行方法
1. 変更をステージする:
   `git add <files>`
2. ビルドした実行ファイルを実行:
   `./aireviewr`
3. 出力:
   - コンソールにレビュー結果が表示されます。
   - .aireviewr/review_result.txt に結果が保存されます。
   - デバッグビルドでは .aireviewr/debug_response_<timestamp>.txt が生成されることがあります。

主要な設定箇所（ソース内）
- モデル名: main.cpp の `#define MODEL_NAME "qwen3.5:9b"` を変更して他モデルを利用できます。
    - rtx 3060 であれば gemma 3:4b で 10秒ほどで応答が返ってきます。
    - thinking 対応のモデルは推論に時間がかかる傾向があります。
- データディレクトリ: `csDataDir`（デフォルト `.aireviewr`）
- Ollama API のエンドポイントは main.cpp 内の curl コマンドで固定: `http://localhost:11434/api/generate`（必要に応じて変更してください）

期待されるレスポンス形式
- 現在の実装は API の返す生データを処理します。モデルに対して特定のフォーマット（例: JSON）の出力を要求するためには、main.cpp の `buildPrompt` を編集して厳密な出力ルールを追記してください。
- 既知の改善案: プロンプトで厳密な JSON 出力を明示し、受信側で JSON のパース（およびフォールバック）を行うことでフォーマットを統一できます。

トラブルシューティング
- Ollama が起動していない／接続できない:
  - Ollama が稼働しているか確認し、`ollama ps` でモデルが起動しているか確認してください。
- curl が見つからない:
  - システムに curl をインストールするか、main.cpp の呼び出し方法を変更してください。
- Windows のコンパイルで C4996 (localtime の警告) が出る:
  - 対処方法は 2 通りあります。
    1. 安全な関数を使用する（推奨）: `localtime_s` に置き換える（ソース修正）。
    2. 非推奨警告を無効化する: プロジェクトのプリプロセッサ定義に `_CRT_SECURE_NO_WARNINGS` を追加する（__C/C++ > Preprocessor > Preprocessor Definitions__）。
- 権限／セキュリティ:
  - 外部コマンド実行や一時ファイル操作が含まれるため、実運用では入力のサニタイズや一時ファイルの安全な取り扱い（排他制御や適切なパーミッション）を検討してください。

ライセンス
- 本リポジトリのコードに適用するライセンス情報があればここに記載してください（デフォルトで記載がなければプロジェクト作成者に確認してください）。

開発メモ
- 実装に関する簡単な説明は main.cpp の先頭コメントを参照してください。
