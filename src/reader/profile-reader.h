#pragma once

// OBS の中で動いている設定を読み、判定の入力を組み立てる。
//
// 配信前は配信の出力が無く、基本モードのエンコーダーは既定値しか持たない。そのため、
// プロファイルの設定と streamEncoder.json とエンコーダーの既定値から組み立て、
// 配信先サービスの推奨値を OBS と同じ条件で適用する。
// 根拠は docs/reports/2026-11-10-bootstrap-survey.md。

#include "core/diagnosis.h"

#include <string>

namespace shindan {

struct Snapshot {
	bool simpleMode = true;
	std::string encoderId;
	std::string codec;

	int width = 0;
	int height = 0;
	double fps = 0;

	// 設定の値
	int configVideoBitrate = 0;
	std::string configRateControl;
	int configKeyint = 0;
	std::string configProfile;
	int configAudioBitrate = 0;

	// サービスの推奨値を適用したあとの、配信で使われる値
	int effectiveVideoBitrate = 0;
	std::string effectiveRateControl;
	int effectiveKeyint = 0;
	std::string effectiveProfile;
	int effectiveAudioBitrate = 0;

	int audioSampleRate = 0;
	std::string channelSetup;
	bool audioSurround = false;
	int audioTrackIndex = 1;

	bool ignoreRecommended = false;
	bool applyServiceSettings = true;
	bool multitrack = false;

	std::string serviceId;
	std::string serviceName;
	std::string serverUrl;
	std::string detectedPlatform; // "youtube"、"twitch"、判別できないときは空

	// OBS が実際に動かしている値。設定を変えて再起動していない間は設定とずれる
	int runtimeWidth = 0;
	int runtimeHeight = 0;
	double runtimeFps = 0;
	int runtimeSampleRate = 0;

	// 配信中と録画中だけ埋まる。OBS が配信用のエンコーダーへ実際に適用した設定で、
	// 組み立てた effective との突き合わせに使う (tools/verify-reader.py)。
	// 録画中の値が配信用のエンコーダーを表すのは、録画の設定が「配信と同じ」のときだけ
	std::string liveSource; // "streaming"、"recording"。どちらでもなければ空
	std::string liveEncoderId;
	int liveVideoBitrate = 0;
	std::string liveRateControl;
	int liveKeyint = 0;
	std::string liveProfile;
	int liveAudioBitrate = 0;
};

Snapshot readSnapshot();

// 読んだ値を、出所ごとに OBS のログへ書く
void logSnapshot(const Snapshot &snapshot, const char *trigger);

DiagnosisInput toDiagnosisInput(const Snapshot &snapshot);

} // namespace shindan
