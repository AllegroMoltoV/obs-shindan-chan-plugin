# 初期調査: OBS診断ちゃんプラグイン

## はじめに

`.prompts/INIT.md` の要件を受け、着手前に確かめるべき事項を調べた結果です。調査日は 2026-11-10 です。INIT.md の調査結果の節に書かれた事実は繰り返さず、新たに分かった事実だけを書きます。

調査の範囲は次のとおりです。

- OBS Studio 33.0.1 のソース: `D:/work/obs-loudness-balance-monitor/.deps/obs-studio-33.0.1/`
- 依頼者の既存プラグイン: `D:/work/obs-loudness-balance-monitor` の `origin/main` (8306652, v1.0.3)、`D:/work/obs-comment-skin`、ほか `D:/work/` 配下のプラグイン
- 既存のアプリ: `D:/work/obs-shindan-chan`
- 倉庫くんの条件: `D:/work/workspace-plugin-distributer/.appendix/20261110/2026111002-init-obs-simple-burst-lines.md` の 141 行から 202 行
- Twitch と YouTube の公式の案内: 末尾の出典を参照

OBS のソースの行番号は、上記のディレクトリからの相対パスで示します。

## 調査結果

### 配信前に設定を読む方法

配信前には `obs_frontend_get_streaming_output()` が NULL を返します。配信の出力を作るのは基本モードと詳細モードのどちらでも `SetupStreaming` の中だけです (`frontend/utility/SimpleOutput.cpp:647-648`、`frontend/utility/AdvancedOutput.cpp:665`)。

エンコーダーは `obs_get_encoder_by_name()` で名前から取得できます (`libobs/obs.h:810`)。ただし、基本モードのエンコーダー `simple_video_stream` は設定なしで作られます (`SimpleOutput.cpp:76`)。設定が入るのは配信・録画・仮想カメラの開始時です (`SimpleOutput.cpp:585`、`616`)。それまでは、エンコーダーの既定値しか読めません。

このため、配信前の値はプロファイルの設定と `streamEncoder.json` から組み立てます。組み立てに使う事実は次のとおりです。

- `obs_frontend_get_profile_config()` は `config_t*` を返します (`frontend/api/obs-frontend-api.h:210`)。OBS が既定値を登録しているため、未設定のキーでも既定値を読めます (`OBSBasic.cpp:626-769`)。
- `streamEncoder.json` には既定値との差分しか保存されません (`libobs/obs-data.c:578`、`749`)。読む側で `obs_get_encoder_defaults(id)` を重ねます。
- 詳細モードで配信に使う音声のトラックは `AdvOut.TrackIndex` で決まります (`AdvancedOutput.cpp:176`)。INIT.md の表にある `AdvOut.Track1Bitrate` ではなく、`Track{TrackIndex}Bitrate` を読みます。

### 配信先のサービスが上書きする値

OBS は配信先のサービスの推奨値を、エンコーダーに適用します。適用する関数は `obs_service_apply_encoder_settings` です。`rtmp_common` のサービスでは、キーフレームの間隔・`rate_control=CBR`・プロファイルを設定します。ビットレートが上限を超えていれば上限に切り詰めます (`plugins/rtmp-services/rtmp-common.c:721-753`)。

- 基本モードでは、映像と音声の `rate_control="CBR"` がコードに固定されています (`SimpleOutput.cpp:333`、`340`)。キーフレームの間隔の固定値はコードに無く、サービスの推奨値 2 秒が適用されます。
- 詳細モードでは `AdvOut.ApplyServiceSettings` の既定値が true です (`OBSBasic.cpp:649`)。利用者の設定したキーフレームの間隔のほうが短いときは、利用者の値が優先されます (`AdvancedOutput.cpp:236-239`)。
- `data/services.json` では、Twitch の推奨値がキーフレームの間隔 2 秒、ビットレートの上限 6,000kbps です。YouTube RTMPS の上限は 51,000kbps です。`rtmp-services` は `services.json` をネットワークから更新するため、実際に使われる値は実機で確かめます。

### コーデックの判別

エンコーダーの ID を `obs_get_encoder_codec(id)` に渡すと、`h264`・`hevc`・`av1` のような文字列が返ります (`libobs/obs-encoder.c:897-906`)。基本モードの `SimpleOutput.StreamEncoder` の値から ID への対応は `get_simple_output_encoder` にあります (`SimpleOutput.cpp:91-126`)。詳細モードでは `AdvOut.Encoder` に ID がそのまま入ります (`OBSBasic.cpp:653`)。

### 配信先の自動判別

`rtmp_common` のサービスでは、設定の `service` キーにサービス名が入ります (`rtmp-common.c:151`)。OBS 自身は `"YouTube - RTMP"`・`"YouTube - RTMPS"`・`"YouTube - HLS"` との一致で YouTube を判定し (`frontend/oauth/YoutubeAuth.hpp:5-7`)、`"Twitch"` との一致で Twitch を判定しています (`frontend/settings/OBSBasicSettings_Stream.cpp:751`)。カスタムのサーバー `rtmp_custom` と WHIP `whip_custom` では、URL のホスト名から推定するほかありません。

### 設定と実際の値がずれる場面

- 映像: 出力が動いている間、`ResetVideo` は何もしません (`OBSBasic.cpp:1442-1444`)。映像の設定ページも無効になります。
- 音声: `ResetAudio` を呼ぶのは起動時だけです (`OBSBasic.cpp:881`)。サンプルレートとチャンネルを変えて保存してから OBS を再起動するまで、`obs_get_audio_info()` の値と設定の値が食い違います。

### 設定の画面を閉じたことの検知

frontend のイベントの一覧 (`frontend/api/obs-frontend-api.h:17-69`) に、設定に関するイベントはありません。設定の画面は、オブジェクト名 `OBSBasicSettings` のモーダルの QDialog です (`frontend/forms/OBSBasicSettings.ui:4`、`OBSBasic_MainControls.cpp:216-219`)。`qApp->installEventFilter` でこのダイアログの `QEvent::Hide` を拾えば、閉じたときに診断をやり直せる見込みです。Hide が確実に届くことは未確認です。

### 既存のプラグインの構成

obs-loudness-balance-monitor の事実は次のとおりです。

- obs-plugintemplate の履歴を引き継いでいます。`cmake/`・`.github/actions/*`・`.github/scripts/`・`.clang-format`・`.gersemirc` はテンプレート由来です。
- `buildspec.json` は OBS 33.0.1 と obs-deps 2026-10-23 です。`bundleId` はテンプレートの値のままです。
- ドックは `OBS_FRONTEND_EVENT_FINISHED_LOADING` で作り、`obs_frontend_add_dock_by_id()` で登録します (`src/plugin-main.cpp:38`)。
- 単体テストはテストフレームワークを使わず、`int main` で書いています (`.github/tests/*.cpp`)。ctest を走らせるのは特定のブランチ向けのワークフローだけで、通常の `build-project.yaml` では走りません。
- `push.yaml` のリリース作成は `1.0.1` のような数字だけのタグに反応します。既存のプラグインでは `v1.0.1` の形のタグを使い、リリースはタグの CI の成果物から別に作っています。

obs-comment-skin もドックの形で OBS 33.0.1 を使い、ctest を通常の CI で走らせています。完了条件に単体テストの CI での合格があるため、CI のテストの組み込みは obs-comment-skin を参考にします。obs-comment-skin には git のリモートがありません。

### ロゴと画像

- 既存のプラグインでは、`docs/images/logo.svg` を原本にし、512×512 の RGBA の PNG に書き出して `.soko-kun/logo.png` に置いています。SVG は 256 の座標系で、#141414 の角丸の正方形を背景にしています。
- 書き出しには Chrome のヘッドレスのスクリーンショットを使っています。手順は `D:/work/obs-voice-spotlight/docs/reports/2026-11-10-bootstrap-survey.md` の 72 行にあります。
- 案は `.appendix/logo-proposals/` に 3 つ作り、依頼者の選択を受けています。
- 倉庫くんのロゴの条件は 256KB 以内・1,024px 以内で、SVG は使えません。説明の画像は 1.5MB 以内・4,096px 以内です。既存のプラグインの説明の画像には GIF が多く使われています。

### 既存のアプリの画像

- `src/assets/ok.png`・`warn.png`・`ng.png` は 128×128 の RGBA の PNG です。2025-05-16 のコミット `6505760` で、作者 allegromoltov が現在の画像に差し替えています。
- README に画像の作者やクレジットの記述は見つかりませんでした。実際の作者と作り方の記録は見つかっていません。

### Twitch の映像のビットレートの上限

broadcasting-guidelines のページを Chrome で描画して読み直しました。映像のビットレートの上限の記述は見つかりませんでした。Maximum の語が付くのは音声の 160kbps だけです。

- 2k のページ (stream-quality) では、Enhanced Broadcasting を使う 2k の配信で、1440p の HEVC を 7.5Mbps から 9Mbps に、1080p の AVC を 6Mbps から 7.5Mbps に上げたと書かれています。上限とは明言されていません。同じページの FAQ の記述と食い違いがあります。
- Dual Format のページの数値は、全トラックの合計の帯域の要件です。1 本あたりの上限の記述はありません。
- https://stream.twitch.tv/encoding は名前解決に失敗し、読めませんでした。調査不能です。
- 一方で、OBS の `services.json` では Twitch の上限が 6,000kbps で、サービスの推奨値を適用するとこの値に切り詰められます。

YouTube のページを読み直した範囲では、最大のビットレートと H.264 のプロファイルの記述は見つかりませんでした。この結果は WebFetch の抽出によるもので、表の数値は原文と照合していません。

## 考察

INIT.md の作業の進め方の手順 3 で確かめる事項は、次の 5 つに絞れると考えています。

1. 配信前の値を、プロファイルの設定と `streamEncoder.json` とエンコーダーの既定値から組み立て、設定の画面の値と一致させられること
2. サービスの推奨値の適用を考慮した、実際に配信で使われる値の扱い。診断には、サービスが上書きした後の値を使うのが利用者の実感に合うと考えています
3. `service` キーによる YouTube と Twitch の判別
4. 設定の画面の Hide を拾えること
5. 配信中の値と、一度配信を止めたあとの値

Twitch の判定は、公式に上限が無く、OBS がサービスの推奨値を適用すると 6,000kbps に切り詰めます。このため、INIT.md の U2 の案 A が実態に合うと考えています。

## 出典

- https://help.twitch.tv/s/article/broadcasting-guidelines?language=en_US
- https://help.twitch.tv/s/article/stream-quality?language=en_US
- https://help.twitch.tv/s/article/dual-format-vertical-video?language=en_US
- https://support.google.com/youtube/answer/2853702?hl=en
- `D:/work/obs-loudness-balance-monitor/docs/reports/2026-11-07-v100-release.md` の 35 行から 43 行
