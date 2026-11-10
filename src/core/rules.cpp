#include "core/rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace shindan {

namespace {

std::string trim(const std::string &s)
{
	size_t b = 0;
	size_t e = s.size();
	while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
		++b;
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		--e;
	return s.substr(b, e - b);
}

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

std::vector<std::string> splitList(const std::string &value)
{
	std::vector<std::string> out;
	std::stringstream ss(value);
	std::string item;
	while (std::getline(ss, item, ',')) {
		item = trim(item);
		if (!item.empty())
			out.push_back(item);
	}
	return out;
}

bool toInt(const std::string &s, int &out)
{
	if (s.empty())
		return false;
	char *end = nullptr;
	long v = std::strtol(s.c_str(), &end, 10);
	if (*end != '\0')
		return false;
	out = static_cast<int>(v);
	return true;
}

bool toDouble(const std::string &s, double &out)
{
	if (s.empty())
		return false;
	char *end = nullptr;
	double v = std::strtod(s.c_str(), &end);
	if (*end != '\0')
		return false;
	out = v;
	return true;
}

bool applyKey(PlatformRules &p, const std::string &key, const std::string &value)
{
	auto intKey = [&](int &dst) {
		return toInt(value, dst);
	};
	if (key == "name")
		p.name = value;
	else if (key == "checked")
		p.checked = value;
	else if (key == "url")
		p.url = value;
	else if (key == "max_height")
		return intKey(p.maxHeight);
	else if (key == "max_fps")
		return toDouble(value, p.maxFps);
	else if (key == "fps_allowed") {
		p.fpsAllowed.clear();
		for (const auto &item : splitList(value)) {
			double fps = 0;
			if (!toDouble(item, fps))
				return false;
			p.fpsAllowed.push_back(fps);
		}
	} else if (key == "video_bitrate_max")
		return intKey(p.videoBitrateMax);
	else if (key == "keyint_min")
		return intKey(p.keyintMin);
	else if (key == "keyint_max")
		return intKey(p.keyintMax);
	else if (key == "keyint_recommended")
		return intKey(p.keyintRecommended);
	else if (key == "profiles") {
		p.profiles.clear();
		for (const auto &item : splitList(value))
			p.profiles.push_back(lower(item));
	} else if (key == "audio_sample_rates") {
		p.audioSampleRates.clear();
		for (const auto &item : splitList(value)) {
			int rate = 0;
			if (!toInt(item, rate))
				return false;
			p.audioSampleRates.push_back(rate);
		}
	} else if (key == "audio_surround_sample_rate")
		return intKey(p.audioSurroundSampleRate);
	else if (key == "audio_surround_allowed") {
		std::string v = lower(value);
		if (v != "true" && v != "false")
			return false;
		p.audioSurroundAllowed = v == "true";
	} else if (key == "audio_stereo_bitrate_min")
		return intKey(p.audioStereoBitrateMin);
	else if (key == "audio_surround_bitrate_min")
		return intKey(p.audioSurroundBitrateMin);
	else if (key == "audio_bitrate_max")
		return intKey(p.audioBitrateMax);
	else if (key == "audio_bitrate_recommended")
		return intKey(p.audioBitrateRecommended);
	else if (key == "multitrack_auto") {
		std::string v = lower(value);
		if (v != "true" && v != "false")
			return false;
		p.multitrackAuto = v == "true";
	} else
		return false;
	return true;
}

bool parseVideoRow(const std::string &line, VideoRow &row)
{
	std::stringstream ss(line);
	std::string h, fps, codec, minK, recK, extra;
	if (!(ss >> h >> fps >> codec >> minK >> recK) || (ss >> extra))
		return false;
	if (!toInt(h, row.height) || !toInt(fps, row.fpsBucket) || !toInt(minK, row.minKbps) ||
	    !toInt(recK, row.recommendedKbps))
		return false;
	if (row.fpsBucket != 30 && row.fpsBucket != 60)
		return false;
	row.codecGroup = lower(codec);
	return row.codecGroup == "h264" || row.codecGroup == "hevc_av1" || row.codecGroup == "any";
}

} // namespace

const PlatformRules *RuleSet::find(const std::string &id) const
{
	for (const auto &p : platforms) {
		if (p.id == id)
			return &p;
	}
	return nullptr;
}

std::optional<RuleSet> parseRules(const std::string &text, std::string &error)
{
	RuleSet set;
	std::stringstream ss(text);
	std::string raw;
	int lineNo = 0;
	PlatformRules *current = nullptr;
	bool inVideo = false;

	auto fail = [&](const std::string &why) {
		error = "rules.ini:" + std::to_string(lineNo) + ": " + why;
		return std::nullopt;
	};

	while (std::getline(ss, raw)) {
		++lineNo;
		std::string line = trim(raw);
		if (lineNo == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF)
			line = trim(line.substr(3)); // UTF-8 の BOM
		if (line.empty() || line[0] == '#')
			continue;

		if (line.front() == '[') {
			if (line.back() != ']')
				return fail("section header is not closed");
			std::string name = lower(trim(line.substr(1, line.size() - 2)));
			std::string id = name;
			inVideo = false;
			const std::string suffix = ".video";
			if (name.size() > suffix.size() &&
			    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
				id = name.substr(0, name.size() - suffix.size());
				inVideo = true;
			}
			current = nullptr;
			for (auto &p : set.platforms) {
				if (p.id == id)
					current = &p;
			}
			if (!current) {
				if (inVideo)
					return fail("video section before platform section: " + id);
				set.platforms.push_back(PlatformRules{});
				current = &set.platforms.back();
				current->id = id;
			}
			continue;
		}

		if (!current)
			return fail("entry outside of a section");

		if (inVideo) {
			VideoRow row;
			if (!parseVideoRow(line, row))
				return fail("invalid video row");
			current->video.push_back(row);
			continue;
		}

		size_t eq = line.find('=');
		if (eq == std::string::npos)
			return fail("missing '='");
		std::string key = lower(trim(line.substr(0, eq)));
		std::string value = trim(line.substr(eq + 1));
		if (!applyKey(*current, key, value))
			return fail("invalid value for key: " + key);
	}

	if (set.platforms.empty()) {
		lineNo = 0;
		return fail("no platform section");
	}
	return set;
}

} // namespace shindan
