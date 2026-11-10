#pragma once

// 8 項目の判定。OBS にも Qt にも依存しない。
// 入力は、配信先のサービスの推奨値を適用したあとの、実際に配信で使われる値とする。

#include "core/rules.h"

#include <array>
#include <string>

namespace shindan {

enum class OutputMode { Simple, Advanced };

struct DiagnosisInput {
	OutputMode mode = OutputMode::Simple;
	int width = 0;
	int height = 0;
	double fps = 0;
	std::string codec; // "h264"、"hevc"、"av1"。判別できないときは空
	int videoBitrateKbps = 0;
	std::string rateControl; // 詳細モードだけで使う
	int keyintSec = 0;       // 0 は自動
	std::string profile;     // 空はエンコーダーの既定
	int audioSampleRate = 0;
	bool audioSurround = false;
	int audioBitrateKbps = 0;
	bool multitrack = false; // Enhanced Broadcasting
};

enum class Item {
	Resolution,
	Fps,
	VideoBitrate,
	RateControl,
	Keyint,
	Profile,
	AudioSampleRate,
	AudioBitrate,
};
constexpr size_t kItemCount = 8;

enum class Status {
	Ok,      // 基準に合う
	Warn,    // 直したほうがよい。ランプの色の判定に数える
	Auto,    // 基本モードのため OBS に任せてよい
	Info,    // 配信先の指定が無い、または判定の対象外
	Manual,  // 表に無い組合せやコーデック不明のため、手で確かめてほしい
	Pending, // Enhanced Broadcasting のためサーバーが決める
};

enum class Lamp { Green, Yellow, Red };

struct ItemResult {
	Item item = Item::Resolution;
	Status status = Status::Info;
	// ロケールのキーの末尾。"Comment.<項目>.<reason>" の形で引く
	std::string reason;
	// 目安の表示に使う数値。意味は reason ごとに決まる
	int expectedMin = 0;
	int expectedRecommended = 0;
	int expectedMax = 0;
};

struct DiagnosisResult {
	std::array<ItemResult, kItemCount> items;
	int warnCount = 0;
	Lamp lamp = Lamp::Green;
};

const char *itemKey(Item item);
Lamp lampForWarnCount(int warnCount);
int fpsBucket(double fps);
std::string codecGroup(const std::string &codec);

DiagnosisResult diagnose(const PlatformRules &rules, const DiagnosisInput &input);

} // namespace shindan
