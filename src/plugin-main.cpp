#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "shindan-dock.h"

#include <QByteArray>
#include <QMainWindow>

#include <util/config-file.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static const char *DOCK_ID = "obs-shindan-chan-dock";

static shindan::ShindanDock *g_dock = nullptr;

static void register_dock()
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window)
		return;

	g_dock = new shindan::ShindanDock();
	g_dock->setObjectName(DOCK_ID);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("DockTitle"), g_dock)) {
		obs_log(LOG_ERROR, "Failed to add dock");
		delete g_dock;
		g_dock = nullptr;
		return;
	}
	obs_log(LOG_INFO, "Dock registered successfully");

	// OBS は DockState をこのイベントの前に復元し、後から足したドックを隠すため、もう一度当てる
	config_t *user_config = obs_frontend_get_user_config();
	if (user_config) {
		const char *saved_state = config_get_string(user_config, "BasicWindow", "DockState");
		if (saved_state && *saved_state) {
			QByteArray dock_state = QByteArray::fromBase64(QByteArray(saved_state));
			if (!main_window->restoreState(dock_state))
				obs_log(LOG_WARNING, "Failed to restore OBS dock state after registration");
		}
	}
}

static void on_frontend_event(enum obs_frontend_event event, void *private_data)
{
	UNUSED_PARAMETER(private_data);

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		register_dock();
		if (g_dock) {
			g_dock->resetPlatformSelection();
			g_dock->rediagnose("finished_loading");
		}
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		if (g_dock) {
			g_dock->resetPlatformSelection();
			g_dock->rediagnose("profile_changed");
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		if (g_dock)
			g_dock->rediagnose("streaming_starting");
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		// 配信中のエンコーダーの実際の設定をログに残す。B1 の突き合わせに使う
		if (g_dock)
			g_dock->rediagnose("streaming_started");
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		// 録画でも配信用のエンコーダーへ推奨値が適用されるため、配信をせずに突き合わせられる
		if (g_dock)
			g_dock->rediagnose("recording_started");
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (g_dock)
			g_dock->rediagnose("streaming_stopped");
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		g_dock = nullptr;
		break;
	default:
		break;
	}
}

extern "C" bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	return true;
}

extern "C" void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	// ドックのウィジェットは OBS が破棄する
	g_dock = nullptr;
	obs_log(LOG_INFO, "plugin unloaded");
}
