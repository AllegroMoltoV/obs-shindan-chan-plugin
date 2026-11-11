"""読み取りの処理の自動検証 (フェーズ B の B1)。

obs-websocket で OBS の設定を組合せごとに変え、録画を短く行う。録画の設定を「配信と同じ」にすると、
録画の開始で OBS は配信用のエンコーダーへ配信先の推奨値を適用する。プラグインは録画の開始時に、
組み立てた値 (effective) と、配信用のエンコーダーの実際の設定 (live) を同じ [snapshot] に出す。
このスクリプトは OBS のログからその 2 つを読み、項目ごとに突き合わせる。

前提:
- OBS がプロファイル test で起動しており、プラグインが導入されている。
- obs-websocket が有効。パスワードは %AppData%/obs-studio/plugin_config/obs-websocket/config.json から読む。
- プロファイル test は実行前に退避しておく。このスクリプトは test の設定を書き換える。

使い方:
    .venv/Scripts/python tools/verify-reader.py --profile test --record-dir <録画の出力先> --report <結果の md>
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import json
import os
import re
import sys
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path

import websockets

APPDATA = Path(os.environ["APPDATA"])
OBS_DIR = APPDATA / "obs-studio"
WS_CONFIG = OBS_DIR / "plugin_config" / "obs-websocket" / "config.json"

COMPARED = ("bitrate", "rate_control", "keyint_sec", "profile", "audio_bitrate")


class Obs:
    def __init__(self, ws):
        self.ws = ws

    @classmethod
    async def connect(cls, port: int) -> "Obs":
        config = json.loads(WS_CONFIG.read_text(encoding="utf-8"))
        ws = await websockets.connect(f"ws://127.0.0.1:{port}", max_size=None)
        hello = json.loads(await ws.recv())
        identify = {"rpcVersion": 1, "eventSubscriptions": 0}
        auth = hello["d"].get("authentication")
        if auth:
            password = config["server_password"]
            secret = base64.b64encode(hashlib.sha256((password + auth["salt"]).encode()).digest()).decode()
            identify["authentication"] = base64.b64encode(
                hashlib.sha256((secret + auth["challenge"]).encode()).digest()
            ).decode()
        await ws.send(json.dumps({"op": 1, "d": identify}))
        identified = json.loads(await ws.recv())
        if identified.get("op") != 2:
            raise RuntimeError(f"identify failed: {identified}")
        return cls(ws)

    async def call(self, request_type: str, data: dict | None = None) -> dict:
        request_id = str(uuid.uuid4())
        await self.ws.send(
            json.dumps({"op": 6, "d": {"requestType": request_type, "requestId": request_id, "requestData": data or {}}})
        )
        while True:
            message = json.loads(await self.ws.recv())
            if message.get("op") == 7 and message["d"]["requestId"] == request_id:
                status = message["d"]["requestStatus"]
                if not status["result"]:
                    raise RuntimeError(f"{request_type} failed: {status}")
                return message["d"].get("responseData", {})

    async def set_param(self, category: str, name: str, value: str) -> None:
        await self.call(
            "SetProfileParameter",
            {"parameterCategory": category, "parameterName": name, "parameterValue": value},
        )


@dataclass
class Scenario:
    name: str
    mode: str  # "Simple" か "Advanced"
    service: str  # "twitch"、"youtube"、"custom"
    encoder: str  # 基本モードは SimpleOutput.StreamEncoder、詳細モードは AdvOut.Encoder
    bitrate: int = 8000
    ignore_recommended: bool = False
    apply_service: bool = True
    adv_settings: dict = field(default_factory=dict)  # 詳細モードの streamEncoder.json の中身

    @property
    def expected_platform(self) -> str:
        return {"twitch": "twitch", "youtube": "youtube", "custom": ""}[self.service]


ADV_DIRTY = {"rate_control": "VBR", "keyint_sec": 4, "profile": "baseline"}

SCENARIOS = [
    Scenario("S1 基本・Twitch・x264", "Simple", "twitch", "x264"),
    Scenario("S2 基本・Twitch・x264・推奨無視", "Simple", "twitch", "x264", ignore_recommended=True),
    Scenario("S3 基本・YouTube・x264", "Simple", "youtube", "x264"),
    Scenario("S4 基本・カスタム・x264", "Simple", "custom", "x264"),
    Scenario("S5 基本・Twitch・NVENC", "Simple", "twitch", "nvenc"),
    Scenario("A1 詳細・Twitch・x264・適用オン", "Advanced", "twitch", "obs_x264", adv_settings=ADV_DIRTY),
    Scenario(
        "A2 詳細・Twitch・x264・適用オフ", "Advanced", "twitch", "obs_x264", apply_service=False, adv_settings=ADV_DIRTY
    ),
    Scenario(
        "A3 詳細・Twitch・x264・適用オン・推奨無視・キーフレーム 0",
        "Advanced",
        "twitch",
        "obs_x264",
        ignore_recommended=True,
        adv_settings={**ADV_DIRTY, "keyint_sec": 0},
    ),
    Scenario("A4 詳細・YouTube・NVENC・適用オン", "Advanced", "youtube", "obs_nvenc_h264_tex", adv_settings=ADV_DIRTY),
    Scenario("A5 詳細・カスタム・x264・適用オン", "Advanced", "custom", "obs_x264", adv_settings=ADV_DIRTY),
]

SERVICES = {
    "twitch": ("rtmp_common", {"service": "Twitch", "server": "auto", "key": "verify-reader-dummy"}),
    "youtube": (
        "rtmp_common",
        {"service": "YouTube - RTMPS", "server": "rtmps://a.rtmps.youtube.com:443/live2", "key": "verify-reader-dummy"},
    ),
    "custom": ("rtmp_custom", {"server": "rtmp://127.0.0.1/verify-reader", "key": "verify-reader-dummy"}),
}


def latest_log() -> Path:
    logs = sorted((OBS_DIR / "logs").glob("*.txt"), key=lambda p: p.stat().st_mtime)
    if not logs:
        raise RuntimeError("OBS のログが見つからない")
    return logs[-1]


SNAPSHOT_RE = re.compile(r"\[snapshot\] (.*)$")


def parse_kv(text: str) -> dict:
    return dict(re.findall(r"(\w+)=(\S*)", text))


def read_recording_snapshot(log: Path, offset: int) -> dict | None:
    """offset 以降のログから trigger=recording_started の [snapshot] を 1 組読む。"""
    data = log.read_bytes()[offset:].decode("utf-8", errors="replace")
    lines = [m.group(1) for m in (SNAPSHOT_RE.search(l) for l in data.splitlines()) if m]
    for i, line in enumerate(lines):
        if line.startswith("trigger=recording_started"):
            block = {"head": parse_kv(line)}
            for follow in lines[i + 1 : i + 8]:
                if follow.startswith("trigger="):
                    break
                kind = follow.split(" ", 1)[0]
                block[kind] = parse_kv(follow)
            return block
    return None


def compare(block: dict) -> list[tuple[str, str, str, bool]]:
    effective = block.get("effective", {})
    live = block.get("live", {})
    rows = []
    for key in COMPARED:
        e = effective.get(key, "")
        l = live.get(key, "")
        same = e.lower() == l.lower()
        rows.append((key, e, l, same))
    return rows


async def apply_scenario(obs: Obs, sc: Scenario, profile: str, other_profile: str, profile_dir: Path) -> None:
    await obs.set_param("Output", "Mode", sc.mode)
    await obs.set_param("Stream1", "IgnoreRecommended", "true" if sc.ignore_recommended else "false")
    await obs.set_param("Stream1", "EnableMultitrackVideo", "false")
    if sc.mode == "Simple":
        await obs.set_param("SimpleOutput", "StreamEncoder", sc.encoder)
        await obs.set_param("SimpleOutput", "VBitrate", str(sc.bitrate))
        await obs.set_param("SimpleOutput", "ABitrate", "160")
        await obs.set_param("SimpleOutput", "RecQuality", "Stream")
    else:
        await obs.set_param("AdvOut", "Encoder", sc.encoder)
        await obs.set_param("AdvOut", "ApplyServiceSettings", "true" if sc.apply_service else "false")
        await obs.set_param("AdvOut", "TrackIndex", "1")
        await obs.set_param("AdvOut", "Track1Bitrate", "160")
        await obs.set_param("AdvOut", "RecType", "Standard")
        await obs.set_param("AdvOut", "RecEncoder", "none")
        await obs.set_param("AdvOut", "RecAudioEncoder", "none")
        settings = {"bitrate": sc.bitrate, **sc.adv_settings}
        (profile_dir / "streamEncoder.json").write_text(json.dumps(settings), encoding="utf-8")
    service_type, service_settings = SERVICES[sc.service]
    await obs.call(
        "SetStreamServiceSettings", {"streamServiceType": service_type, "streamServiceSettings": service_settings}
    )
    # エンコーダーの種類とモードの変更を出力へ反映させるため、プロファイルを切り替えて戻す (ResetOutputs)
    await obs.call("SetCurrentProfile", {"profileName": other_profile})
    await asyncio.sleep(1.5)
    await obs.call("SetCurrentProfile", {"profileName": profile})
    await asyncio.sleep(1.5)


async def run(args) -> int:
    obs = await Obs.connect(args.port)
    profiles = await obs.call("GetProfileList")
    original = profiles["currentProfileName"]
    if args.profile not in profiles["profiles"]:
        raise RuntimeError(f"profile {args.profile} not found")
    other = next(p for p in profiles["profiles"] if p != args.profile)
    await obs.call("SetCurrentProfile", {"profileName": args.profile})
    await asyncio.sleep(1.5)
    profile_dir = OBS_DIR / "basic" / "profiles" / args.profile
    await obs.call("SetRecordDirectory", {"recordDirectory": str(Path(args.record_dir).resolve())})

    results = []
    for sc in SCENARIOS:
        await apply_scenario(obs, sc, args.profile, other, profile_dir)
        log = latest_log()
        offset = log.stat().st_size
        await obs.call("StartRecord")
        await asyncio.sleep(3)
        await obs.call("StopRecord")
        await asyncio.sleep(1.5)
        block = read_recording_snapshot(log, offset)
        results.append((sc, block))
        print(f"{sc.name}: {'snapshot found' if block else 'snapshot NOT found'}", flush=True)

    await obs.call("SetCurrentProfile", {"profileName": original})
    write_report(Path(args.report), results)
    failures = sum(1 for sc, b in results if not judge(sc, b)[0])
    print(f"{len(results)} scenarios, {failures} failures")
    return 0 if failures == 0 else 1


def judge(sc: Scenario, block: dict | None) -> tuple[bool, str]:
    if not block:
        return False, "録画の開始の [snapshot] がログに無い"
    live = block.get("live", {})
    if live.get("source") != "recording":
        return False, f"live の出所が recording ではない ({live.get('source', 'none')})"
    mismatched = [k for k, _, _, same in compare(block) if not same]
    platform = block["head"].get("platform", "")
    notes = []
    # SetProfileParameter とプロファイルの切替えで設定が反映されたかを、プラグインが読んだモードで確かめる
    if block["head"].get("mode") != sc.mode:
        notes.append(f"モードが反映されていない ({block['head'].get('mode')})")
    if mismatched:
        notes.append("不一致: " + "、".join(mismatched))
    if platform != sc.expected_platform:
        notes.append(f"配信先の判別: 期待 {sc.expected_platform or '(なし)'}、結果 {platform or '(なし)'}")
    return not notes, "；".join(notes) or "一致"


def write_report(path: Path, results) -> None:
    lines = [
        "# 読み取りの自動検証の結果",
        "",
        f"実行日時: {time.strftime('%Y-%m-%d %H:%M:%S')}。スクリプト: `tools/verify-reader.py`。",
        "",
        "| 組合せ | 判定 | 内容 |",
        "| --- | --- | --- |",
    ]
    for sc, block in results:
        ok, note = judge(sc, block)
        lines.append(f"| {sc.name} | {'合格' if ok else '不合格'} | {note} |")
    lines.append("")
    for sc, block in results:
        lines.append(f"## {sc.name}")
        lines.append("")
        if not block:
            lines.append("[snapshot] が見つからなかった。")
            lines.append("")
            continue
        head = block["head"]
        lines.append(
            f"mode={head.get('mode')} encoder={head.get('encoder')} codec={head.get('codec')} "
            f"service={head.get('service_id')} platform={head.get('platform') or '(なし)'}"
        )
        lines.append("")
        lines.append("| 項目 | config | effective | live | 一致 |")
        lines.append("| --- | --- | --- | --- | --- |")
        config = block.get("config", {})
        for key, e, l, same in compare(block):
            lines.append(f"| {key} | {config.get(key, '')} | {e} | {l} | {'一致' if same else '不一致'} |")
        lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=4455)
    parser.add_argument("--profile", default="test")
    parser.add_argument("--record-dir", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()
    return asyncio.run(run(args))


if __name__ == "__main__":
    sys.exit(main())
