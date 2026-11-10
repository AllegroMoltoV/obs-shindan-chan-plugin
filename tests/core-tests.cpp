// src/core/ の単体テスト。配布する data/rules.ini をそのまま読み、境界値・表に無い組合せ・
// コーデック不明・Enhanced Broadcasting の扱いを確かめる。

#include "core/diagnosis.h"
#include "core/rules.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace shindan;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const char *expr, const char *file, int line)
{
	++g_checks;
	if (!cond) {
		++g_failures;
		std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
	}
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

RuleSet loadRules()
{
	std::ifstream f(SHINDAN_RULES_PATH, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	std::string error;
	auto rules = parseRules(ss.str(), error);
	if (!rules) {
		std::fprintf(stderr, "cannot parse %s: %s\n", SHINDAN_RULES_PATH, error.c_str());
		std::exit(2);
	}
	return *rules;
}

DiagnosisInput advanced1080p60()
{
	DiagnosisInput in;
	in.mode = OutputMode::Advanced;
	in.width = 1920;
	in.height = 1080;
	in.fps = 60;
	in.codec = "h264";
	in.videoBitrateKbps = 6000;
	in.rateControl = "CBR";
	in.keyintSec = 2;
	in.profile = "high";
	in.audioSampleRate = 48000;
	in.audioBitrateKbps = 160;
	return in;
}

const ItemResult &item(const DiagnosisResult &r, Item it)
{
	return r.items[static_cast<size_t>(it)];
}

void testParseShippedRules(const RuleSet &set)
{
	const PlatformRules *yt = set.find("youtube");
	const PlatformRules *tw = set.find("twitch");
	CHECK(yt != nullptr);
	CHECK(tw != nullptr);
	if (!yt || !tw)
		return;
	CHECK(yt->video.size() == 20);
	CHECK(tw->video.size() == 4);
	CHECK(yt->checked == "2026-11-10");
	CHECK(tw->profiles.size() == 2);
	CHECK(tw->multitrackAuto);
	CHECK(!yt->multitrackAuto);
}

void testParseErrors()
{
	std::string error;
	CHECK(!parseRules("[x]\nname YouTube\n", error));
	CHECK(error.find(":2:") != std::string::npos);
	CHECK(!parseRules("[x]\nunknown_key = 1\n", error));
	CHECK(!parseRules("[x]\nmax_height = abc\n", error));
	CHECK(!parseRules("[x.video]\n1080 60 h264 0 6000\n", error));
	CHECK(!parseRules("[x]\n[x.video]\n1080 45 h264 0 6000\n", error));
	CHECK(!parseRules("[x]\n[x.video]\n1080 60 vp9 0 6000\n", error));
	CHECK(!parseRules("[x]\n[x.video]\n1080 60 h264 0\n", error));
	CHECK(!parseRules("# only comment\n", error));
	CHECK(!parseRules("[x\n", error));
	auto ok = parseRules("\xEF\xBB\xBF[x]\r\nmax_height = 1080\r\n", error);
	CHECK(ok.has_value());
	if (ok)
		CHECK(ok->platforms[0].maxHeight == 1080);
}

void testYouTubeVideoBitrate(const PlatformRules &yt)
{
	DiagnosisInput in = advanced1080p60();
	in.videoBitrateKbps = 6000; // H.264 の最小ちょうど
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Ok);
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).expectedRecommended == 17000);
	in.videoBitrateKbps = 5999;
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Warn);
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).reason == "TooLow");

	in.codec = "hevc";
	in.videoBitrateKbps = 4000; // AV1・HEVC の最小ちょうど
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Ok);
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).expectedRecommended == 12000);
	in.codec = "av1";
	in.videoBitrateKbps = 3999;
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Warn);

	// U1: 上限を作らない
	in.codec = "h264";
	in.videoBitrateKbps = 100000;
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Ok);

	// コーデック不明は判定しない
	in.codec = "";
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Manual);
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).reason == "UnknownCodec");
}

void testResolutionAndTable(const PlatformRules &yt, const PlatformRules &tw)
{
	DiagnosisInput in = advanced1080p60();
	// 縦型の配信は短辺で引く
	in.width = 1080;
	in.height = 1920;
	CHECK(item(diagnose(yt, in), Item::Resolution).status == Status::Ok);
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).expectedRecommended == 17000);

	// 表に無い解像度
	in.width = 1600;
	in.height = 900;
	CHECK(item(diagnose(yt, in), Item::Resolution).status == Status::Manual);
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Manual);
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).reason == "NotInTable");

	// 上限を超える解像度
	in.width = 5120;
	in.height = 2880;
	CHECK(item(diagnose(yt, in), Item::Resolution).status == Status::Warn);
	in.width = 2560;
	in.height = 1440;
	CHECK(item(diagnose(yt, in), Item::Resolution).status == Status::Ok);
	CHECK(item(diagnose(tw, in), Item::Resolution).status == Status::Warn);

	// 480p は YouTube の表にあり、30 の区分だけがある
	in.width = 854;
	in.height = 480;
	in.fps = 30;
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).expectedMin == 400);
	in.fps = 60;
	CHECK(item(diagnose(yt, in), Item::VideoBitrate).status == Status::Manual);

	// 解像度が読めないとき
	in.width = 0;
	in.height = 0;
	CHECK(item(diagnose(yt, in), Item::Resolution).status == Status::Manual);
}

void testFps(const PlatformRules &yt, const PlatformRules &tw)
{
	DiagnosisInput in = advanced1080p60();
	in.fps = 59.94;
	CHECK(fpsBucket(in.fps) == 60);
	CHECK(item(diagnose(yt, in), Item::Fps).status == Status::Ok);
	CHECK(item(diagnose(tw, in), Item::Fps).status == Status::Ok);
	in.fps = 29.97;
	CHECK(fpsBucket(in.fps) == 30);
	CHECK(item(diagnose(tw, in), Item::Fps).status == Status::Ok);
	in.fps = 48;
	CHECK(item(diagnose(tw, in), Item::Fps).status == Status::Warn);
	CHECK(item(diagnose(yt, in), Item::Fps).status == Status::Ok);
	in.fps = 120;
	CHECK(item(diagnose(yt, in), Item::Fps).status == Status::Warn);
	in.fps = 0;
	CHECK(item(diagnose(yt, in), Item::Fps).status == Status::Manual);
}

void testTwitchBitrateAndMultitrack(const PlatformRules &tw)
{
	DiagnosisInput in = advanced1080p60();
	in.videoBitrateKbps = 6000;
	CHECK(item(diagnose(tw, in), Item::VideoBitrate).status == Status::Ok);
	in.videoBitrateKbps = 6001;
	CHECK(item(diagnose(tw, in), Item::VideoBitrate).status == Status::Warn);
	CHECK(item(diagnose(tw, in), Item::VideoBitrate).reason == "TooHigh");
	// Twitch の表はコーデックを問わない
	in.codec = "";
	in.videoBitrateKbps = 4500;
	CHECK(item(diagnose(tw, in), Item::VideoBitrate).status == Status::Ok);

	// Enhanced Broadcasting では映像の項目を保留し、警告に数えない
	DiagnosisInput eb = advanced1080p60();
	eb.videoBitrateKbps = 9000;
	eb.rateControl = "VBR";
	eb.width = 2560;
	eb.height = 1440;
	eb.multitrack = true;
	eb.audioBitrateKbps = 320;
	DiagnosisResult r = diagnose(tw, eb);
	CHECK(item(r, Item::Resolution).status == Status::Pending);
	CHECK(item(r, Item::VideoBitrate).status == Status::Pending);
	CHECK(item(r, Item::RateControl).status == Status::Pending);
	CHECK(item(r, Item::Keyint).status == Status::Pending);
	CHECK(item(r, Item::Profile).status == Status::Pending);
	CHECK(item(r, Item::AudioBitrate).status == Status::Warn);
	CHECK(r.warnCount == 1);
}

void testSimpleMode(const PlatformRules &yt)
{
	DiagnosisInput in = advanced1080p60();
	in.mode = OutputMode::Simple;
	in.rateControl = "VBR";
	in.keyintSec = 0;
	in.profile = "baseline";
	DiagnosisResult r = diagnose(yt, in);
	CHECK(item(r, Item::RateControl).status == Status::Auto);
	CHECK(item(r, Item::Keyint).status == Status::Auto);
	CHECK(item(r, Item::Profile).status == Status::Auto);
	CHECK(r.warnCount == 0);
	CHECK(r.lamp == Lamp::Green);
}

void testAdvancedEncoderItems(const PlatformRules &yt, const PlatformRules &tw)
{
	DiagnosisInput in = advanced1080p60();
	in.rateControl = "cbr";
	CHECK(item(diagnose(yt, in), Item::RateControl).status == Status::Ok);
	in.rateControl = "VBR";
	CHECK(item(diagnose(yt, in), Item::RateControl).status == Status::Warn);
	in.rateControl = "";
	CHECK(item(diagnose(yt, in), Item::RateControl).status == Status::Manual);

	in = advanced1080p60();
	in.keyintSec = 0;
	CHECK(item(diagnose(yt, in), Item::Keyint).status == Status::Warn);
	CHECK(item(diagnose(yt, in), Item::Keyint).reason == "Zero");
	in.keyintSec = 4;
	CHECK(item(diagnose(yt, in), Item::Keyint).status == Status::Ok);
	in.keyintSec = 5;
	CHECK(item(diagnose(yt, in), Item::Keyint).status == Status::Warn);
	in.keyintSec = 1;
	CHECK(item(diagnose(tw, in), Item::Keyint).status == Status::Warn);
	in.keyintSec = 2;
	CHECK(item(diagnose(tw, in), Item::Keyint).status == Status::Ok);

	in = advanced1080p60();
	in.profile = "High";
	CHECK(item(diagnose(tw, in), Item::Profile).status == Status::Ok);
	in.profile = "baseline";
	CHECK(item(diagnose(tw, in), Item::Profile).status == Status::Warn);
	in.profile = "";
	CHECK(item(diagnose(tw, in), Item::Profile).status == Status::Info);
	in.profile = "baseline";
	CHECK(item(diagnose(yt, in), Item::Profile).status == Status::Info);
	in.codec = "hevc";
	CHECK(item(diagnose(tw, in), Item::Profile).reason == "NotH264");
}

void testAudio(const PlatformRules &yt, const PlatformRules &tw)
{
	DiagnosisInput in = advanced1080p60();
	in.audioBitrateKbps = 128;
	CHECK(item(diagnose(yt, in), Item::AudioBitrate).status == Status::Ok);
	in.audioBitrateKbps = 127;
	CHECK(item(diagnose(yt, in), Item::AudioBitrate).status == Status::Warn);

	in.audioSurround = true;
	in.audioSampleRate = 44100;
	in.audioBitrateKbps = 384;
	CHECK(item(diagnose(yt, in), Item::AudioSampleRate).status == Status::Warn);
	CHECK(item(diagnose(yt, in), Item::AudioSampleRate).reason == "SurroundNeeds");
	CHECK(item(diagnose(yt, in), Item::AudioBitrate).status == Status::Ok);
	in.audioBitrateKbps = 383;
	CHECK(item(diagnose(yt, in), Item::AudioBitrate).status == Status::Warn);
	CHECK(item(diagnose(tw, in), Item::AudioBitrate).reason == "SurroundNotAllowed");

	in = advanced1080p60();
	in.audioBitrateKbps = 160;
	CHECK(item(diagnose(tw, in), Item::AudioBitrate).status == Status::Ok);
	in.audioBitrateKbps = 161;
	CHECK(item(diagnose(tw, in), Item::AudioBitrate).status == Status::Warn);
	CHECK(item(diagnose(tw, in), Item::AudioSampleRate).status == Status::Info);
	in.audioSampleRate = 32000;
	CHECK(item(diagnose(yt, in), Item::AudioSampleRate).status == Status::Warn);
}

void testLamp()
{
	CHECK(lampForWarnCount(0) == Lamp::Green);
	CHECK(lampForWarnCount(1) == Lamp::Yellow);
	CHECK(lampForWarnCount(2) == Lamp::Yellow);
	CHECK(lampForWarnCount(3) == Lamp::Red);
	CHECK(lampForWarnCount(8) == Lamp::Red);
}

} // namespace

int main()
{
	RuleSet set = loadRules();
	testParseShippedRules(set);
	testParseErrors();
	const PlatformRules *yt = set.find("youtube");
	const PlatformRules *tw = set.find("twitch");
	if (yt && tw) {
		testYouTubeVideoBitrate(*yt);
		testResolutionAndTable(*yt, *tw);
		testFps(*yt, *tw);
		testTwitchBitrateAndMultitrack(*tw);
		testSimpleMode(*yt);
		testAdvancedEncoderItems(*yt, *tw);
		testAudio(*yt, *tw);
	}
	testLamp();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
