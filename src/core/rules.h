#pragma once

// 判定の基準 (data/rules.ini) の読み込み。OBS にも Qt にも依存しない。

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace shindan {

struct VideoRow {
	int height = 0;         // 短辺 (px)
	int fpsBucket = 0;      // 30 か 60
	std::string codecGroup; // "h264"、"hevc_av1"、"any"
	int minKbps = 0;        // 0 は指定なし
	int recommendedKbps = 0;
};

struct PlatformRules {
	std::string id; // "youtube"、"twitch"
	std::string name;
	std::string checked;
	std::string url;
	int maxHeight = 0;
	double maxFps = 0;
	std::vector<double> fpsAllowed;
	int videoBitrateMax = 0;
	int keyintMin = 0;
	int keyintMax = 0;
	int keyintRecommended = 0;
	std::vector<std::string> profiles;
	std::vector<int> audioSampleRates;
	int audioSurroundSampleRate = 0;
	bool audioSurroundAllowed = true;
	int audioStereoBitrateMin = 0;
	int audioSurroundBitrateMin = 0;
	int audioBitrateMax = 0;
	int audioBitrateRecommended = 0;
	bool multitrackAuto = false;
	std::vector<VideoRow> video;
};

struct RuleSet {
	std::vector<PlatformRules> platforms;
	const PlatformRules *find(const std::string &id) const;
};

// 解析に失敗したときは std::nullopt を返し、error に行番号つきの理由を書く。
std::optional<RuleSet> parseRules(const std::string &text, std::string &error);

} // namespace shindan
