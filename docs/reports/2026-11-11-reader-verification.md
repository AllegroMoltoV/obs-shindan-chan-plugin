# 読み取りの自動検証の結果

実行日時: 2026-11-11 16:47:42。スクリプト: `tools/verify-reader.py`。

## 検証方法

プラグインが配信の前に組み立てた値 (effective) と、OBS が配信用のエンコーダーへ実際に適用した値 (live) を突き合わせました。live は、録画の設定を「配信と同じ」にして録画を 3 秒行い、録画の開始時に配信用のエンコーダーから読みました。録画の開始でも、OBS は配信先の推奨値を配信用のエンコーダーへ適用します (`SimpleOutput.cpp:792`、`AdvancedOutput.cpp:803`)。

検証環境は、Windows 11 の OBS Studio 33.0.1、プロファイル `test`、プラグインは main の `b5e3ac5` の CI の成果物です。設定の変更は obs-websocket で行い、配信キーは使っていません。

配信先の推奨値による書き換えが起きる組合せを含めました。S1・S5・A1 では設定の 8,000kbps が 6,000kbps に、A1 では VBR が CBR に、キーフレームの間隔 4 秒が 2 秒に書き換わり、effective と live の両方がその値でした。S2・A3 では推奨を無視する設定でビットレートが 8,000kbps のまま残り、A2 では適用をオフにした設定がそのまま残りました。

確かめていない範囲は次のとおりです。

- 本物の配信。配信先のサーバーとの接続後に起きる挙動と、Enhanced Broadcasting でサーバーが決める設定
- 基本モードで、音声のビットレートに選択肢にない値を入れた場合の丸め
- 設定の画面を閉じたときの自動再診断。使い心地のテストの間の OBS のログで確かめる
- 音声のサンプルレートの変更から再起動までの表示

## 検証結果

| 組合せ | 判定 | 内容 |
| --- | --- | --- |
| S1 基本・Twitch・x264 | 合格 | 一致 |
| S2 基本・Twitch・x264・推奨無視 | 合格 | 一致 |
| S3 基本・YouTube・x264 | 合格 | 一致 |
| S4 基本・カスタム・x264 | 合格 | 一致 |
| S5 基本・Twitch・NVENC | 合格 | 一致 |
| A1 詳細・Twitch・x264・適用オン | 合格 | 一致 |
| A2 詳細・Twitch・x264・適用オフ | 合格 | 一致 |
| A3 詳細・Twitch・x264・適用オン・推奨無視・キーフレーム 0 | 合格 | 一致 |
| A4 詳細・YouTube・NVENC・適用オン | 合格 | 一致 |
| A5 詳細・カスタム・x264・適用オン | 合格 | 一致 |

## S1 基本・Twitch・x264

mode=Simple encoder=obs_x264 codec=h264 service=rtmp_common platform=twitch

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 6000 | 6000 | 一致 |
| rate_control | CBR | CBR | CBR | 一致 |
| keyint_sec | 0 | 2 | 2 | 一致 |
| profile |  |  |  | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## S2 基本・Twitch・x264・推奨無視

mode=Simple encoder=obs_x264 codec=h264 service=rtmp_common platform=twitch

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 8000 | 8000 | 一致 |
| rate_control | CBR | CBR | CBR | 一致 |
| keyint_sec | 0 | 2 | 2 | 一致 |
| profile |  |  |  | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## S3 基本・YouTube・x264

mode=Simple encoder=obs_x264 codec=h264 service=rtmp_common platform=youtube

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 8000 | 8000 | 一致 |
| rate_control | CBR | CBR | CBR | 一致 |
| keyint_sec | 0 | 2 | 2 | 一致 |
| profile |  |  |  | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## S4 基本・カスタム・x264

mode=Simple encoder=obs_x264 codec=h264 service=rtmp_custom platform=(なし)

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 8000 | 8000 | 一致 |
| rate_control | CBR | CBR | CBR | 一致 |
| keyint_sec | 0 | 0 | 0 | 一致 |
| profile |  |  |  | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## S5 基本・Twitch・NVENC

mode=Simple encoder=obs_nvenc_h264_tex codec=h264 service=rtmp_common platform=twitch

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 6000 | 6000 | 一致 |
| rate_control | CBR | CBR | CBR | 一致 |
| keyint_sec | 0 | 2 | 2 | 一致 |
| profile | high | high | high | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## A1 詳細・Twitch・x264・適用オン

mode=Advanced encoder=obs_x264 codec=h264 service=rtmp_common platform=twitch

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 6000 | 6000 | 一致 |
| rate_control | VBR | CBR | CBR | 一致 |
| keyint_sec | 4 | 2 | 2 | 一致 |
| profile | baseline | baseline | baseline | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## A2 詳細・Twitch・x264・適用オフ

mode=Advanced encoder=obs_x264 codec=h264 service=rtmp_common platform=twitch

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 8000 | 8000 | 一致 |
| rate_control | VBR | VBR | VBR | 一致 |
| keyint_sec | 4 | 4 | 4 | 一致 |
| profile | baseline | baseline | baseline | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## A3 詳細・Twitch・x264・適用オン・推奨無視・キーフレーム 0

mode=Advanced encoder=obs_x264 codec=h264 service=rtmp_common platform=twitch

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 8000 | 8000 | 一致 |
| rate_control | VBR | CBR | CBR | 一致 |
| keyint_sec | 0 | 2 | 2 | 一致 |
| profile | baseline | baseline | baseline | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## A4 詳細・YouTube・NVENC・適用オン

mode=Advanced encoder=obs_nvenc_h264_tex codec=h264 service=rtmp_common platform=youtube

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 8000 | 8000 | 一致 |
| rate_control | VBR | CBR | CBR | 一致 |
| keyint_sec | 4 | 2 | 2 | 一致 |
| profile | baseline | baseline | baseline | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

## A5 詳細・カスタム・x264・適用オン

mode=Advanced encoder=obs_x264 codec=h264 service=rtmp_custom platform=(なし)

| 項目 | config | effective | live | 一致 |
| --- | --- | --- | --- | --- |
| bitrate | 8000 | 8000 | 8000 | 一致 |
| rate_control | VBR | VBR | VBR | 一致 |
| keyint_sec | 4 | 4 | 4 | 一致 |
| profile | baseline | baseline | baseline | 一致 |
| audio_bitrate | 160 | 160 | 160 | 一致 |

