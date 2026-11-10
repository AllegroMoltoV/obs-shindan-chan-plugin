#include "reader/profile-reader.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/config-file.h>

#include <cstring>
#include <string>

namespace shindan {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

int cfgInt(config_t *cfg, const char *section, const char *name)
{
	return static_cast<int>(config_get_int(cfg, section, name));
}

double configFps(config_t *cfg)
{
	// OBSBasic::GetConfigFPS と同じ読み方
	uint64_t type = config_get_uint(cfg, "Video", "FPSType");
	if (type == 1)
		return static_cast<double>(config_get_uint(cfg, "Video", "FPSInt"));
	if (type == 2) {
		uint64_t num = config_get_uint(cfg, "Video", "FPSNum");
		uint64_t den = config_get_uint(cfg, "Video", "FPSDen");
		return den ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
	}
	std::string common = str(config_get_string(cfg, "Video", "FPSCommon"));
	if (common == "10")
		return 10;
	if (common == "20")
		return 20;
	if (common == "24 NTSC")
		return 24000.0 / 1001.0;
	if (common == "25 PAL")
		return 25;
	if (common == "29.97")
		return 30000.0 / 1001.0;
	if (common == "48")
		return 48;
	if (common == "50 PAL")
		return 50;
	if (common == "59.94")
		return 60000.0 / 1001.0;
	if (common == "60")
		return 60;
	return 30;
}

bool contains(const std::string &haystack, const char *needle)
{
	return haystack.find(needle) != std::string::npos;
}

std::string detectPlatform(const std::string &serviceId, const std::string &serviceName, const std::string &url)
{
	// OBS 自身の判定 (frontend/oauth/YoutubeAuth.hpp、OBSBasicSettings_Stream.cpp) と同じ名前で判別する
	if (serviceId == "rtmp_common") {
		if (serviceName == "YouTube - RTMP" || serviceName == "YouTube - RTMPS" ||
		    serviceName == "YouTube - HLS")
			return "youtube";
		if (serviceName == "Twitch")
			return "twitch";
	}
	// カスタムのサーバーと WHIP は URL のホスト名から推定する
	if (contains(url, "youtube.com") || contains(url, "youtube.googleapis.com"))
		return "youtube";
	if (contains(url, "twitch.tv") || contains(url, "contribute.live-video.net"))
		return "twitch";
	return "";
}

void readEncoderSettings(obs_data_t *settings, int &bitrate, std::string &rateControl, int &keyint,
			 std::string &profile)
{
	bitrate = static_cast<int>(obs_data_get_int(settings, "bitrate"));
	rateControl = str(obs_data_get_string(settings, "rate_control"));
	keyint = static_cast<int>(obs_data_get_int(settings, "keyint_sec"));
	profile = str(obs_data_get_string(settings, "profile"));
}

// 既定値を下敷きにした設定の写しを作る。id が空なら空の設定を返す
obs_data_t *withDefaults(const std::string &encoderId, obs_data_t *overrides)
{
	obs_data_t *data = encoderId.empty() ? obs_data_create() : obs_encoder_defaults(encoderId.c_str());
	if (!data)
		data = obs_data_create();
	if (overrides)
		obs_data_apply(data, overrides);
	return data;
}

void readSimple(config_t *cfg, obs_service_t *service, Snapshot &s)
{
	obs_encoder_t *enc = obs_get_encoder_by_name("simple_video_stream");
	if (enc) {
		s.encoderId = str(obs_encoder_get_id(enc));
		obs_encoder_release(enc);
	}

	s.configVideoBitrate = cfgInt(cfg, "SimpleOutput", "VBitrate");
	s.configAudioBitrate = cfgInt(cfg, "SimpleOutput", "ABitrate");
	s.configRateControl = "CBR"; // SimpleOutput::Update で固定
	s.configKeyint = 0;

	// SimpleOutput::Update と同じ手順で、配信に使われる値を組み立てる
	obs_data_t *video = obs_data_create();
	obs_data_t *audio = obs_data_create();
	obs_data_set_string(video, "rate_control", "CBR");
	obs_data_set_int(video, "bitrate", s.configVideoBitrate);
	obs_data_set_string(audio, "rate_control", "CBR");
	obs_data_set_int(audio, "bitrate", s.configAudioBitrate);
	if (service)
		obs_service_apply_encoder_settings(service, video, audio);
	if (s.ignoreRecommended) {
		obs_data_set_int(video, "bitrate", s.configVideoBitrate);
		obs_data_set_int(audio, "bitrate", s.configAudioBitrate);
	}

	obs_data_t *effective = withDefaults(s.encoderId, video);
	readEncoderSettings(effective, s.effectiveVideoBitrate, s.effectiveRateControl, s.effectiveKeyint,
			    s.effectiveProfile);
	s.effectiveAudioBitrate = static_cast<int>(obs_data_get_int(audio, "bitrate"));

	obs_data_t *configured = withDefaults(s.encoderId, nullptr);
	s.configProfile = str(obs_data_get_string(configured, "profile"));

	obs_data_release(configured);
	obs_data_release(effective);
	obs_data_release(audio);
	obs_data_release(video);
}

void readAdvanced(config_t *cfg, obs_service_t *service, Snapshot &s)
{
	s.encoderId = str(config_get_string(cfg, "AdvOut", "Encoder"));

	obs_data_t *file = nullptr;
	char *profilePath = obs_frontend_get_current_profile_path();
	if (profilePath) {
		std::string path = std::string(profilePath) + "/streamEncoder.json";
		file = obs_data_create_from_json_file_safe(path.c_str(), "bak");
		bfree(profilePath);
	}

	// AdvancedOutput::UpdateStreamSettings と同じ手順
	obs_data_t *configured = withDefaults(s.encoderId, file);
	readEncoderSettings(configured, s.configVideoBitrate, s.configRateControl, s.configKeyint, s.configProfile);

	obs_data_t *effective = withDefaults(s.encoderId, configured);
	if (s.applyServiceSettings && service) {
		obs_service_apply_encoder_settings(service, effective, nullptr);
		if (s.ignoreRecommended)
			obs_data_set_int(effective, "bitrate", s.configVideoBitrate);
		int enforced = static_cast<int>(obs_data_get_int(effective, "keyint_sec"));
		if (s.configKeyint != 0 && s.configKeyint < enforced)
			obs_data_set_int(effective, "keyint_sec", s.configKeyint);
	}
	readEncoderSettings(effective, s.effectiveVideoBitrate, s.effectiveRateControl, s.effectiveKeyint,
			    s.effectiveProfile);

	s.audioTrackIndex = cfgInt(cfg, "AdvOut", "TrackIndex");
	if (s.audioTrackIndex < 1)
		s.audioTrackIndex = 1;
	std::string key = "Track" + std::to_string(s.audioTrackIndex) + "Bitrate";
	s.configAudioBitrate = cfgInt(cfg, "AdvOut", key.c_str());
	s.effectiveAudioBitrate = s.configAudioBitrate; // 詳細モードでは音声に推奨値を適用しない

	obs_data_release(effective);
	obs_data_release(configured);
	obs_data_release(file);
}

void readLive(Snapshot &s)
{
	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return;
	s.streaming = obs_output_active(output);
	if (s.streaming) {
		obs_encoder_t *venc = obs_output_get_video_encoder(output);
		if (venc) {
			s.liveEncoderId = str(obs_encoder_get_id(venc));
			obs_data_t *settings = obs_encoder_get_settings(venc);
			readEncoderSettings(settings, s.liveVideoBitrate, s.liveRateControl, s.liveKeyint,
					    s.liveProfile);
			obs_data_release(settings);
		}
		obs_encoder_t *aenc = obs_output_get_audio_encoder(output, 0);
		if (aenc) {
			obs_data_t *settings = obs_encoder_get_settings(aenc);
			s.liveAudioBitrate = static_cast<int>(obs_data_get_int(settings, "bitrate"));
			obs_data_release(settings);
		}
	}
	obs_output_release(output);
}

} // namespace

Snapshot readSnapshot()
{
	Snapshot s;
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return s;

	s.simpleMode = str(config_get_string(cfg, "Output", "Mode")) != "Advanced";
	s.width = cfgInt(cfg, "Video", "OutputCX");
	s.height = cfgInt(cfg, "Video", "OutputCY");
	s.fps = configFps(cfg);
	s.audioSampleRate = cfgInt(cfg, "Audio", "SampleRate");
	s.channelSetup = str(config_get_string(cfg, "Audio", "ChannelSetup"));
	s.audioSurround = !s.channelSetup.empty() && s.channelSetup != "Mono" && s.channelSetup != "Stereo";
	s.ignoreRecommended = config_get_bool(cfg, "Stream1", "IgnoreRecommended");
	s.applyServiceSettings = config_get_bool(cfg, "AdvOut", "ApplyServiceSettings");
	s.multitrack = config_get_bool(cfg, "Stream1", "EnableMultitrackVideo");

	obs_service_t *service = obs_frontend_get_streaming_service();
	if (service) {
		s.serviceId = str(obs_service_get_id(service));
		obs_data_t *settings = obs_service_get_settings(service);
		s.serviceName = str(obs_data_get_string(settings, "service"));
		obs_data_release(settings);
		s.serverUrl = str(obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL));
	}
	s.detectedPlatform = detectPlatform(s.serviceId, s.serviceName, s.serverUrl);

	if (s.simpleMode)
		readSimple(cfg, service, s);
	else
		readAdvanced(cfg, service, s);

	if (!s.encoderId.empty())
		s.codec = str(obs_get_encoder_codec(s.encoderId.c_str()));

	struct obs_video_info ovi = {};
	if (obs_get_video_info(&ovi)) {
		s.runtimeWidth = static_cast<int>(ovi.output_width);
		s.runtimeHeight = static_cast<int>(ovi.output_height);
		s.runtimeFps = ovi.fps_den ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 0.0;
	}
	struct obs_audio_info oai = {};
	if (obs_get_audio_info(&oai))
		s.runtimeSampleRate = static_cast<int>(oai.samples_per_sec);

	readLive(s);
	return s;
}

void logSnapshot(const Snapshot &s, const char *trigger)
{
	// B1 では、配信前の effective と配信中の live をこのログで突き合わせる
	obs_log(LOG_INFO, "[snapshot] trigger=%s mode=%s encoder=%s codec=%s service_id=%s service=%s platform=%s",
		trigger, s.simpleMode ? "Simple" : "Advanced", s.encoderId.c_str(), s.codec.c_str(),
		s.serviceId.c_str(), s.serviceName.c_str(), s.detectedPlatform.c_str());
	obs_log(LOG_INFO, "[snapshot] flags ignore_recommended=%d apply_service_settings=%d multitrack=%d",
		s.ignoreRecommended, s.applyServiceSettings, s.multitrack);
	obs_log(LOG_INFO, "[snapshot] video config=%dx%d@%.3f runtime=%dx%d@%.3f", s.width, s.height, s.fps,
		s.runtimeWidth, s.runtimeHeight, s.runtimeFps);
	obs_log(LOG_INFO, "[snapshot] config bitrate=%d rate_control=%s keyint_sec=%d profile=%s audio_bitrate=%d",
		s.configVideoBitrate, s.configRateControl.c_str(), s.configKeyint, s.configProfile.c_str(),
		s.configAudioBitrate);
	obs_log(LOG_INFO, "[snapshot] effective bitrate=%d rate_control=%s keyint_sec=%d profile=%s audio_bitrate=%d",
		s.effectiveVideoBitrate, s.effectiveRateControl.c_str(), s.effectiveKeyint, s.effectiveProfile.c_str(),
		s.effectiveAudioBitrate);
	obs_log(LOG_INFO, "[snapshot] audio sample_rate config=%d runtime=%d channels=%s track=%d", s.audioSampleRate,
		s.runtimeSampleRate, s.channelSetup.c_str(), s.audioTrackIndex);
	if (s.streaming) {
		obs_log(LOG_INFO,
			"[snapshot] live encoder=%s bitrate=%d rate_control=%s keyint_sec=%d profile=%s audio_bitrate=%d",
			s.liveEncoderId.c_str(), s.liveVideoBitrate, s.liveRateControl.c_str(), s.liveKeyint,
			s.liveProfile.c_str(), s.liveAudioBitrate);
	} else {
		obs_log(LOG_INFO, "[snapshot] live not streaming");
	}
}

DiagnosisInput toDiagnosisInput(const Snapshot &s)
{
	DiagnosisInput in;
	in.mode = s.simpleMode ? OutputMode::Simple : OutputMode::Advanced;
	in.width = s.width;
	in.height = s.height;
	in.fps = s.fps;
	in.codec = s.codec;
	in.videoBitrateKbps = s.effectiveVideoBitrate;
	in.rateControl = s.effectiveRateControl;
	in.keyintSec = s.effectiveKeyint;
	in.profile = s.effectiveProfile;
	in.audioSampleRate = s.audioSampleRate;
	in.audioSurround = s.audioSurround;
	in.audioBitrateKbps = s.effectiveAudioBitrate;
	in.multitrack = s.multitrack;
	return in;
}

} // namespace shindan
