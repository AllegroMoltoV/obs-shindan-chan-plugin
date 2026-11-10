#include "core/diagnosis.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace shindan {

namespace {

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

int shortSide(const DiagnosisInput &in)
{
	return std::min(in.width, in.height);
}

// 29.97 や 59.94 のような NTSC の値を、30 や 60 と同じものとして扱う
bool fpsMatches(double fps, double allowed)
{
	return std::fabs(fps - allowed) < 0.05 || std::fabs(fps * 1.001 - allowed) < 0.05;
}

bool heightInTable(const PlatformRules &r, int height)
{
	return std::any_of(r.video.begin(), r.video.end(), [&](const VideoRow &row) { return row.height == height; });
}

const VideoRow *findRow(const PlatformRules &r, int height, int bucket, const std::string &group)
{
	for (const auto &row : r.video) {
		if (row.height != height || row.fpsBucket != bucket)
			continue;
		if (row.codecGroup == "any" || row.codecGroup == group)
			return &row;
	}
	return nullptr;
}

bool needsCodec(const PlatformRules &r)
{
	return std::any_of(r.video.begin(), r.video.end(), [](const VideoRow &row) { return row.codecGroup != "any"; });
}

ItemResult make(Item item, Status status, std::string reason, int mn = 0, int rec = 0, int mx = 0)
{
	ItemResult r;
	r.item = item;
	r.status = status;
	r.reason = std::move(reason);
	r.expectedMin = mn;
	r.expectedRecommended = rec;
	r.expectedMax = mx;
	return r;
}

ItemResult judgeResolution(const PlatformRules &r, const DiagnosisInput &in)
{
	const Item it = Item::Resolution;
	int side = shortSide(in);
	if (side <= 0)
		return make(it, Status::Manual, "Unknown");
	if (r.maxHeight > 0 && side > r.maxHeight)
		return make(it, Status::Warn, "TooHigh", 0, 0, r.maxHeight);
	if (!heightInTable(r, side))
		return make(it, Status::Manual, "NotInTable", 0, 0, r.maxHeight);
	return make(it, Status::Ok, "Ok", 0, 0, r.maxHeight);
}

ItemResult judgeFps(const PlatformRules &r, const DiagnosisInput &in)
{
	const Item it = Item::Fps;
	if (in.fps <= 0)
		return make(it, Status::Manual, "Unknown");
	if (!r.fpsAllowed.empty()) {
		bool ok = std::any_of(r.fpsAllowed.begin(), r.fpsAllowed.end(),
				      [&](double a) { return fpsMatches(in.fps, a); });
		return ok ? make(it, Status::Ok, "Ok") : make(it, Status::Warn, "NotAllowed");
	}
	if (r.maxFps > 0 && in.fps > r.maxFps + 0.05)
		return make(it, Status::Warn, "TooHigh", 0, 0, static_cast<int>(r.maxFps));
	return make(it, Status::Ok, "Ok", 0, 0, static_cast<int>(r.maxFps));
}

ItemResult judgeVideoBitrate(const PlatformRules &r, const DiagnosisInput &in)
{
	const Item it = Item::VideoBitrate;
	std::string group = codecGroup(in.codec);
	if (needsCodec(r) && group.empty())
		return make(it, Status::Manual, "UnknownCodec");
	const VideoRow *row = findRow(r, shortSide(in), fpsBucket(in.fps), group);
	if (!row)
		return make(it, Status::Manual, "NotInTable");
	if (row->minKbps > 0 && in.videoBitrateKbps < row->minKbps)
		return make(it, Status::Warn, "TooLow", row->minKbps, row->recommendedKbps, r.videoBitrateMax);
	if (r.videoBitrateMax > 0 && in.videoBitrateKbps > r.videoBitrateMax)
		return make(it, Status::Warn, "TooHigh", row->minKbps, row->recommendedKbps, r.videoBitrateMax);
	return make(it, Status::Ok, "Ok", row->minKbps, row->recommendedKbps, r.videoBitrateMax);
}

ItemResult judgeRateControl(const DiagnosisInput &in)
{
	const Item it = Item::RateControl;
	if (in.mode == OutputMode::Simple)
		return make(it, Status::Auto, "Auto");
	if (in.rateControl.empty())
		return make(it, Status::Manual, "Unknown");
	return lower(in.rateControl) == "cbr" ? make(it, Status::Ok, "Ok") : make(it, Status::Warn, "NotCbr");
}

ItemResult judgeKeyint(const PlatformRules &r, const DiagnosisInput &in)
{
	const Item it = Item::Keyint;
	if (in.mode == OutputMode::Simple)
		return make(it, Status::Auto, "Auto");
	if (in.keyintSec <= 0)
		return make(it, Status::Warn, "Zero", r.keyintMin, r.keyintRecommended, r.keyintMax);
	if ((r.keyintMin > 0 && in.keyintSec < r.keyintMin) || (r.keyintMax > 0 && in.keyintSec > r.keyintMax))
		return make(it, Status::Warn, "OutOfRange", r.keyintMin, r.keyintRecommended, r.keyintMax);
	return make(it, Status::Ok, "Ok", r.keyintMin, r.keyintRecommended, r.keyintMax);
}

ItemResult judgeProfile(const PlatformRules &r, const DiagnosisInput &in)
{
	const Item it = Item::Profile;
	if (in.mode == OutputMode::Simple)
		return make(it, Status::Auto, "Auto");
	if (codecGroup(in.codec) != "h264")
		return make(it, Status::Info, "NotH264");
	if (r.profiles.empty())
		return make(it, Status::Info, "NoRule");
	if (in.profile.empty())
		return make(it, Status::Info, "EncoderDefault");
	std::string p = lower(in.profile);
	bool ok = std::find(r.profiles.begin(), r.profiles.end(), p) != r.profiles.end();
	return ok ? make(it, Status::Ok, "Ok") : make(it, Status::Warn, "NotAllowed");
}

ItemResult judgeSampleRate(const PlatformRules &r, const DiagnosisInput &in)
{
	const Item it = Item::AudioSampleRate;
	if (in.audioSampleRate <= 0)
		return make(it, Status::Manual, "Unknown");
	if (in.audioSurround && r.audioSurroundSampleRate > 0 && in.audioSampleRate != r.audioSurroundSampleRate)
		return make(it, Status::Warn, "SurroundNeeds", 0, r.audioSurroundSampleRate);
	if (r.audioSampleRates.empty())
		return make(it, Status::Info, "NoRule");
	bool ok = std::find(r.audioSampleRates.begin(), r.audioSampleRates.end(), in.audioSampleRate) !=
		  r.audioSampleRates.end();
	return ok ? make(it, Status::Ok, "Ok") : make(it, Status::Warn, "NotAllowed");
}

ItemResult judgeAudioBitrate(const PlatformRules &r, const DiagnosisInput &in)
{
	const Item it = Item::AudioBitrate;
	if (in.audioBitrateKbps <= 0)
		return make(it, Status::Manual, "Unknown");
	if (in.audioSurround && !r.audioSurroundAllowed)
		return make(it, Status::Warn, "SurroundNotAllowed");
	int mn = in.audioSurround ? r.audioSurroundBitrateMin : r.audioStereoBitrateMin;
	int rec = in.audioSurround ? r.audioSurroundBitrateMin : r.audioBitrateRecommended;
	if (mn > 0 && in.audioBitrateKbps < mn)
		return make(it, Status::Warn, "TooLow", mn, rec, r.audioBitrateMax);
	if (r.audioBitrateMax > 0 && in.audioBitrateKbps > r.audioBitrateMax)
		return make(it, Status::Warn, "TooHigh", mn, rec, r.audioBitrateMax);
	return make(it, Status::Ok, "Ok", mn, rec, r.audioBitrateMax);
}

} // namespace

const char *itemKey(Item item)
{
	switch (item) {
	case Item::Resolution:
		return "Resolution";
	case Item::Fps:
		return "Fps";
	case Item::VideoBitrate:
		return "VideoBitrate";
	case Item::RateControl:
		return "RateControl";
	case Item::Keyint:
		return "Keyint";
	case Item::Profile:
		return "Profile";
	case Item::AudioSampleRate:
		return "AudioSampleRate";
	case Item::AudioBitrate:
		return "AudioBitrate";
	}
	return "";
}

Lamp lampForWarnCount(int warnCount)
{
	if (warnCount <= 0)
		return Lamp::Green;
	if (warnCount <= 2)
		return Lamp::Yellow;
	return Lamp::Red;
}

int fpsBucket(double fps)
{
	return fps > 30.5 ? 60 : 30;
}

std::string codecGroup(const std::string &codec)
{
	std::string c = lower(codec);
	if (c == "h264")
		return "h264";
	if (c == "hevc" || c == "av1")
		return "hevc_av1";
	return "";
}

DiagnosisResult diagnose(const PlatformRules &rules, const DiagnosisInput &input)
{
	DiagnosisResult result;
	result.items = {judgeResolution(rules, input), judgeFps(rules, input),         judgeVideoBitrate(rules, input),
			judgeRateControl(input),       judgeKeyint(rules, input),      judgeProfile(rules, input),
			judgeSampleRate(rules, input), judgeAudioBitrate(rules, input)};

	// Enhanced Broadcasting では映像の設定をサーバーが決めるため、映像の項目は判定しない
	if (input.multitrack && rules.multitrackAuto) {
		for (auto &item : result.items) {
			if (item.item != Item::AudioSampleRate && item.item != Item::AudioBitrate)
				item = make(item.item, Status::Pending, "Multitrack");
		}
	}

	for (const auto &item : result.items) {
		if (item.status == Status::Warn)
			++result.warnCount;
	}
	result.lamp = lampForWarnCount(result.warnCount);
	return result;
}

} // namespace shindan
