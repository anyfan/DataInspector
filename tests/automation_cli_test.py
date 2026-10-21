"""Real executable + CLI integration, without local datasets or an installed package."""
import json
import contextlib
import importlib.util
import io
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile
from unittest.mock import patch


def main():
    executable = Path(sys.argv[1]).resolve()
    cli = Path(sys.argv[2]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_CONTROLS_STYLE="Fusion",
               QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1")
    for invalid_args in (["--automation-file"], ["--automation-file", ""]):
        proc = subprocess.run([str(executable), *invalid_args], env=env, capture_output=True, timeout=10)
        assert proc.returncode == 2, (invalid_args, proc.returncode, proc.stderr)
    with tempfile.TemporaryDirectory(prefix="automation-cli-", dir=executable.parent.parent) as work:
        root = Path(work)
        connection = root / "connection.json"
        data = root / "数据.csv"
        data.write_text("time,signal\n0,1\n1,3\n2,5\n", encoding="utf-8")
        with (root / "app.log").open("wb") as log:
            app = subprocess.Popen([str(executable), "--automation-file", str(connection), str(data)],
                                   env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 30
                while not connection.exists():
                    if app.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Application did not start: " + (root / "app.log").read_text(encoding="utf-8", errors="replace"))
                    time.sleep(0.05)

                def call(command, params=None, ok=True, use_file=False, request_id=None, wait=False, full=False, ascii_output=False, extra_flags=()):
                    args = [sys.executable, str(cli), "--connection", str(connection), "--timeout", "15", command]
                    args.extend(extra_flags)
                    if ascii_output:
                        args.append("--ascii")
                    if request_id is not None:
                        args.extend(["--request-id", request_id])
                    if wait:
                        args.append("--wait")
                    if params is not None:
                        payload = json.dumps(params, ensure_ascii=False)
                        if use_file:
                            param_file = root / "params.json"
                            param_file.write_text(payload, encoding="utf-8-sig")
                            args.extend(["--params-file", str(param_file)])
                        else:
                            args.extend(["--params", payload])
                    proc = subprocess.run(args, env=env, capture_output=True, encoding="utf-8", timeout=20)
                    if ascii_output:
                        proc.stdout.encode("ascii")  # Every output byte must survive legacy Windows code pages.
                    result = json.loads(proc.stdout)
                    assert proc.returncode == (0 if ok else 1), (proc.returncode, proc.stdout, proc.stderr)
                    assert result["ok"] == ok, result
                    return result if full else result.get("result") if ok else result["error"]

                status = call("wait")
                assert status["signalCount"] == 1, status
                if os.name == "nt":
                    launcher = cli.with_suffix(".ps1")
                    assert launcher.exists(), launcher
                    shell = shutil.which("pwsh") or "powershell.exe"
                    launched = subprocess.run([shell, "-NoProfile", "-File", str(launcher), "--connection", str(connection), "status", "--ascii"],
                        capture_output=True, encoding="utf-8", timeout=30)
                    assert launched.returncode == 0 and not launched.stderr.strip(), (launched.returncode, launched.stdout, launched.stderr)
                    assert json.loads(launched.stdout)["result"]["signalCount"] == 1
                assert len(call("help")) >= 10
                catalog = call("help")
                assert all(isinstance(c["inputSchema"], dict) and isinstance(c["outputSchema"], dict) for c in catalog)
                schemas = {c["command"]: c for c in catalog}
                capabilities = call("capabilities")
                assert capabilities["protocolVersion"] == 1 and capabilities["apiVersion"] == 3
                assert len(capabilities["catalogHash"]) == 64 and set(capabilities["commands"]) == set(schemas)
                assert call("help", full=True)["cli"] == capabilities
                assert call("status")["cli"] == capabilities
                assert json.loads(connection.read_text(encoding="utf-8"))["cli"] == capabilities
                call("status", extra_flags=["--expect-api-version", "3", "--expect-catalog-hash", capabilities["catalogHash"]])
                before_mismatch = call("signals")["total"]
                assert call("clear", extra_flags=["--expect-api-version", "999"], ok=False)["code"] == "version_mismatch"
                assert call("clear", extra_flags=["--expect-catalog-hash", "0" * 64], ok=False, ascii_output=True)["code"] == "version_mismatch"
                assert call("signals")["total"] == before_mismatch
                unknown = call("not_a_command", ok=False)
                assert unknown["code"] == "unknown_command" and "help" in unknown["message"]
                incompatible = root / "incompatible.json"
                descriptor = json.loads(connection.read_text(encoding="utf-8")); descriptor["protocol"] = 99
                incompatible.write_text(json.dumps(descriptor), encoding="utf-8")
                assert call("clear", extra_flags=["--connection", str(incompatible)], ok=False)["code"] == "protocol_mismatch"
                # Older servers lack capabilities; opt-in version checks must stop before the mutation.
                spec = importlib.util.spec_from_file_location("cli_under_test", cli)
                client = importlib.util.module_from_spec(spec)
                with patch.object(sys, "dont_write_bytecode", True):
                    spec.loader.exec_module(client)
                for server_error, expected in [("unknown_command", "version_mismatch"), ("unauthorized", "unauthorized")]:
                    output = io.StringIO()
                    with patch.object(sys, "argv", [str(cli), "--connection", str(connection), "clear", "--expect-api-version", "3", "--ascii"]), \
                         patch.object(client, "request", return_value={"ok": False, "error": {"code": server_error}}) as sent, \
                         contextlib.redirect_stdout(output):
                        assert client.main() == 1
                    assert json.loads(output.getvalue())["error"]["code"] == expected
                    assert sent.call_count == 1 and sent.call_args.args[1] == "capabilities"
                assert "sample" in schemas["nearest"]["outputSchema"]["properties"]
                assert "ready" in schemas["merge_preview"]["taskResultSchema"]["properties"]
                assert "attitudeVisible" in schemas["aircraft_setup"]["taskResultSchema"]["properties"]
                malformed = root / "malformed.csv"
                malformed.write_text('"unterminated header\n', encoding="utf-8")
                failed_load = call("load", {"path": str(malformed)}, ok=False, wait=True, full=True)
                assert failed_load["task"]["state"] == "failed", failed_load
                listing = call("signals")
                params = {"generation": listing["generation"], "signal": 0}
                stats = call("statistics", params)
                assert stats["count"] == 3 and stats["mean"] == 3, stats
                call("layout", {"rows": 2, "columns": 1})
                call("plot_signal", dict(params, plot=1, enabled=True))
                call("rename", dict(params, name="测试名称"), use_file=True)
                assert call("signals")["signals"][0]["name"] == "测试名称"
                params["generation"] = call("signals")["generation"]
                enums = [{"value": 0, "text": "关闭", "color": "#ff687887"},
                         {"value": 1, "text": "开启", "color": "#ff278451"}]
                assert call("signal_enums", params)["enums"] == []
                call("set_signal_enums", dict(params, enums=enums), use_file=True, request_id="set-enums")
                assert call("set_signal_enums", dict(params, enums=enums), request_id="set-enums", full=True)["replayed"]
                params["generation"] = call("signals")["generation"]
                assert call("signal_enums", params)["enums"] == enums
                call("set_signal_enums", dict(params, enums=[enums[0], enums[0]]), ok=False)
                call("set_signal_enums", dict(params, enums=[dict(enums[0], color="invalid")]), ok=False)
                call("set_signal_enums", dict(params, enums=[dict(enums[0], value=0.5)]), ok=False)
                call("set_signal_enums", dict(params, enums=[dict(enums[0], extra=True)]), ok=False)
                call("signal_properties", dict(params, name="不应提交", enums=[enums[0], enums[0]]), ok=False)
                assert call("signals")["signals"][0]["name"] == "测试名称"
                assert call("signal_enums", params)["enums"] == enums
                call("set_signal_enums", dict(params, enums=[]))
                assert call("signal_enums", params)["enums"] == []
                call("signal_properties", dict(params, enums=enums, width=4), use_file=True)
                params["generation"] = call("signals")["generation"]
                assert call("signal_enums", params)["enums"] == enums
                assert call("signals")["signals"][0]["width"] == 4
                # Apply and verify 98 enum tables in one request, including Chinese through ASCII JSON.
                bulk_file = root / "批量.csv"
                bulk_file.write_text("time," + ",".join(f"枚举{i}" for i in range(98)) + "\n0," + ",".join("0" for _ in range(98)) + "\n", encoding="utf-8")
                old_generation = params["generation"]
                call("load", {"path": str(bulk_file)}, wait=True)
                bulk_generation = call("signals")["generation"]
                updates = [{"signal": i + 1, "enums": enums} for i in range(98)]
                bulk_params = {"generation": bulk_generation, "updates": updates}
                assert call("set_signal_enums_batch", dict(bulk_params, generation=old_generation), ok=False)["code"] == "stale_generation"
                applied = call("set_signal_enums_batch", bulk_params, use_file=True, request_id="bulk-98", ascii_output=True)
                assert applied["updated"] == 98 and len(applied["signals"]) == 98
                for i, signal in enumerate(applied["signals"]):
                    assert signal["originalName"] == f"枚举{i}" and signal["enums"] == enums, signal
                assert call("set_signal_enums_batch", bulk_params, use_file=True, request_id="bulk-98", full=True)["replayed"]
                invalid = [{"signal": 1, "enums": []}] + updates[1:-1] + [{"signal": 98, "enums": [dict(enums[0], color="invalid")]}]
                call("set_signal_enums_batch", dict(bulk_params, updates=invalid), use_file=True, ok=False, ascii_output=True)
                call("set_signal_enums_batch", dict(bulk_params, updates=[updates[0], updates[0]]), ok=False)
                call("set_signal_enums_batch", dict(bulk_params, updates=[updates[0], {"signal": 999999, "enums": []}]), ok=False)
                checked = call("signal_enums_batch", {"generation": bulk_generation, "signals": list(range(1, 99))}, ascii_output=True)
                assert checked["signals"] == applied["signals"]  # Invalid final entry did not commit the first one.
                call("signal_enums_batch", {"generation": bulk_generation, "signals": [1, 1]}, ok=False)
                cleared = call("set_signal_enums_batch", dict(bulk_params, updates=[{"signal": i, "enums": []} for i in range(1, 99)]), use_file=True)
                assert all(s["enums"] == [] for s in cleared["signals"])
                call("file_remove", {"generation": bulk_generation, "path": str(bulk_file)})
                params["generation"] = call("signals")["generation"]
                session = root / "保存.disession"
                call("save_session", {"path": str(session)}, use_file=True)
                call("restore_session", {"path": str(session)}, use_file=True)
                status = call("wait")
                assert status["rows"] == 2 and status["columns"] == 1, status
                assert status["plots"][1]["signals"] == [0], status
                params["generation"] = call("signals")["generation"]
                assert call("signal_enums", params)["enums"] == enums
                assert call("nearest", dict(params, time=0.8))["sample"] == [1, 3]
                assert call("samples", dict(params, limit=2))["nextOffset"] == 2
                # Full processing workflow: create group, create/edit/remove rules, preserve references.
                group = call("object_create", {"name": "处理组", "type": "processing"})["object"]
                call("object_rename", {"object": group, "name": "处理组更新"})
                assert next(o for o in call("objects") if o["id"] == group)["name"] == "处理组更新"
                generation = call("signals")["generation"]
                assert call("rule_inputs", {"generation": generation, "object": group})["inputs"]
                scale = {"name": "放大", "operation": "scale", "inputs": ["raw:0"], "factor": 2}
                created = call("rule_create", {"generation": generation, "object": group, "config": scale}, wait=True)
                assert created["completed"] and created["evaluationState"] == "succeeded", created
                assert created["rules"][0]["rawInputSignals"] == [0] and created["rules"][0]["sourceSignals"] == [0]
                assert created["rules"][0]["inputSources"][0]["rawId"] == "raw:0"
                rule = created["outputs"][0]["rule"]
                definition = next(o for o in call("objects") if o["id"] == group)
                output = definition["rules"][0]["outputs"][0]
                generation = call("signals")["generation"]
                assert call("samples", {"generation": generation, "signal": output["series"]})["samples"] == [[0, 2], [1, 6], [2, 10]]
                downstream = call("rule_create", {"generation": generation, "object": group,
                    "config": dict(scale, name="下游", inputs=[output["id"]])}, wait=True)["outputs"][0]["rule"]
                generation = call("signals")["generation"]
                call("rule_remove", {"generation": generation, "object": group, "rule": rule}, ok=False)
                by_source = call("rules", {"generation": generation, "sourceSignal": 0})
                assert by_source["total"] == 2 and all(r["sourceSignals"] == [0] for r in by_source["rules"]), by_source
                downstream_rule = next(r for r in by_source["rules"] if r["id"] == downstream)
                assert downstream_rule["rawInputSignals"] == [] and downstream_rule["inputSources"][0]["kind"] == "output"
                call("rules", {"sourceSignal": 0}, ok=False)
                assert len(next(o for o in call("objects") if o["id"] == group)["rules"]) == 2
                view_path = root / "完整视图.diview"
                call("view_template_save", {"path": str(view_path)}, use_file=True)
                saved_view = json.loads(view_path.read_text(encoding="utf-8"))
                assert saved_view["version"] == 6 and len(saved_view["processing"]) == 1
                preview = call("view_template_preview", {"path": str(view_path)})
                assert len(preview["rows"]) == 1 and preview["ruleCount"] == 2 and preview["outputCount"] == 2, preview
                before_import = call("signals")
                import_params = {"generation": before_import["generation"], "path": str(view_path),
                    "mapping": {str(row["id"]): row["match"] for row in preview["rows"]}}
                call("view_template_apply", dict(import_params, mapping={}), ok=False)
                assert len(call("objects")) == 1
                imported = call("view_template_apply", import_params, wait=True, request_id="import-view")
                assert imported["checkedRules"] == 2 and len(call("objects")) == 2, imported
                assert call("view_template_apply", import_params, request_id="import-view", full=True)["replayed"]
                assert call("samples", {"generation": before_import["generation"], "signal": 0}, ok=False)["code"] == "stale_generation"
                call("view_template_undo")
                after_undo = call("signals")
                assert after_undo["total"] == before_import["total"] and len(call("objects")) == 1
                assert after_undo["generation"] != before_import["generation"]
                generation = after_undo["generation"]
                computed_session = root / "computed.disession"
                call("save_session", {"path": str(computed_session)})
                restored_task = call("restore_session", {"path": str(computed_session)}, wait=True)
                assert restored_task["completed"] and restored_task["checkedRules"] == 2, restored_task
                assert not call("status")["deriving"]
                generation = call("signals")["generation"]
                copy = call("object_duplicate", {"generation": generation, "object": group})["object"]
                call("wait")
                generation = call("signals")["generation"]
                call("object_remove", {"generation": generation, "object": copy})
                call("wait")
                generation = call("signals")["generation"]
                call("rule_remove", {"generation": generation, "object": group, "rule": downstream})
                call("wait")
                generation = call("signals")["generation"]
                call("rule_remove", {"generation": generation, "object": group, "rule": rule})
                call("wait")
                generation = call("signals")["generation"]
                call("object_remove", {"generation": generation, "object": group})
                call("wait")
                general = call("object_create", {"name": "字段测试", "type": "general"})["object"]
                field = call("object_field_add", {"object": general})["field"]
                call("object_field_rename", {"object": general, "field": field, "name": "字段更新"})
                call("object_field_remove", {"object": general, "field": field})
                call("wait")
                call("object_remove", {"generation": call("signals")["generation"], "object": general})
                call("wait")
                bit_group = call("object_create", {"name": "位段", "type": "processing"})["object"]
                bit_rule = call("rule_create", {"generation": call("signals")["generation"], "object": bit_group,
                    "config": {"name": "位段提取", "operation": "bitfield", "inputs": ["raw:0"], "autoWordBits": True, "byteOrder": "little",
                        "outputs": [{"name": "标志位", "startBit": 0, "bitCount": 1, "signedField": False},
                                    {"name": "分类位段", "startBit": 1, "bitCount": 2, "signedField": False}]}}, wait=True)["outputs"][0]["rule"]
                bit_outputs = next(o for o in call("objects") if o["id"] == bit_group)["rules"][0]["outputs"]
                assert len(bit_outputs) == 2
                bit_generation = call("signals")["generation"]
                assert call("samples", {"generation": bit_generation, "signal": bit_outputs[0]["series"]})["samples"] == [[0, 1], [1, 1], [2, 1]]
                assert call("samples", {"generation": bit_generation, "signal": bit_outputs[1]["series"]})["samples"] == [[0, 0], [1, 1], [2, 2]]
                bit_enums = call("set_signal_enums_batch", {"generation": bit_generation, "updates": [
                    {"signal": bit_outputs[0]["series"], "enums": enums}, {"signal": bit_outputs[1]["series"], "enums": enums + [{"value": 2, "text": "维护", "color": "#ff123456"}]}]})
                assert bit_enums["updated"] == 2 and len(bit_enums["signals"][1]["enums"]) == 3
                call("object_remove", {"generation": bit_generation, "object": bit_group})
                call("wait")
                generation = call("signals")["generation"]
                call("rename_reset", {"generation": generation, "signal": 0})
                assert call("signals")["signals"][0]["name"] == "signal"
                call("scope_offset", {"generation": call("signals")["generation"], "signal": 0, "scope": "file", "seconds": 2})
                assert call("signals")["signals"][0]["timeOffset"] == 2
                call("scope_offset", {"generation": call("signals")["generation"], "signal": 0, "scope": "signal", "seconds": -2, "add": True})
                assert call("signals")["signals"][0]["timeOffset"] == 0
                axes = call("plot_axes", {"plot": 1})
                call("plot_axes_set", {"plot": 1, "yMinimum": -10, "yMaximum": 20})
                assert call("plot_axes", {"plot": 1})["yMaximum"] == 20
                call("plot_axes_set", {"plot": 1, "yMinimum": 30, "yMaximum": 20}, ok=False)
                assert call("plot_axes", {"plot": 1})["yMinimum"] == -10
                generation = call("signals")["generation"]
                lane = {"generation": generation, "plot": 1, "lane": -1, "signal": 0, "name": "状态", "visible": True}
                call("state_lane", lane)
                call("state_lane", dict(lane, name="状态二"))
                assert len(call("state_status", {"plot": 1})["lanes"]) == 2
                call("state_lane_move", {"plot": 1, "lane": 1, "direction": -1})
                assert call("state_status", {"plot": 1})["lanes"][0]["name"] == "状态二"
                call("state_lane_move", {"plot": 1, "lane": 0, "direction": 0}, ok=False)
                call("state_lane_remove", {"plot": 1, "lane": 1})
                call("view_mode", {"plot": 1, "mode": "curve"})
                assert not call("state_status", {"plot": 1})["enabled"]
                call("plot_clear", {"plot": 1})
                assert call("status")["plots"][1]["signals"] == []
                call("plot_axes", {"plot": 63}, ok=False)
                unload = root / "unload.csv"
                unload.write_text("time,temp\n0,1\n1,2\n", encoding="utf-8")
                call("load", {"path": str(unload)}, wait=True)
                call("file_remove", {"generation": call("signals")["generation"], "path": str(unload)})
                assert unload.exists() and call("signals")["total"] == 1
                position = root / "position.csv"
                position.write_text("time,lat,lon,height\n0,30,110,100\n1,30.001,110.001,120\n2,30.002,110.002,110\n", encoding="utf-8")
                call("load", {"path": str(position)}, use_file=True)
                call("wait")
                create_params = {"name": "验证飞机", "type": "aircraft"}
                aircraft = call("object_create", create_params, use_file=True, request_id="create-aircraft")["object"]
                replay = call("object_create", create_params, request_id="create-aircraft", full=True)
                assert replay["replayed"] and replay["result"]["object"] == aircraft and len(call("objects")) == 1
                assert call("object_create", dict(create_params, name="different"), request_id="create-aircraft", ok=False)["code"] == "idempotency_conflict"
                config = dict(object=aircraft, geographic=True, attitudeMode=0, radians=False,
                              order=0, scalarLast=False, navigationToBody=False)
                call("aircraft_configure", config)
                definition = next(o for o in call("objects") if o["id"] == aircraft)
                fields = {f["role"]: f["id"] for f in definition["fields"]}
                listing = call("signals")
                sources = {s["name"]: s["signal"] for s in listing["signals"]}
                for role, name in (("latitude", "lat"), ("longitude", "lon"), ("height", "height")):
                    call("object_bind_field", dict(generation=listing["generation"], object=aircraft,
                                                   field=fields[role], signal=sources[name]))
                    call("wait")
                invalid = dict(config, attitudeMode=1.5)
                assert call("aircraft_configure", invalid, ok=False)["code"] == "invalid_params"
                assert next(o for o in call("objects") if o["id"] == aircraft)["attitudeMode"] == 0
                call("object_trajectory", {"object": aircraft, "plot": 0})
                deadline = time.monotonic() + 15
                while True:
                    trajectory = call("trajectory_status", {"plot": 0})
                    if trajectory["ready"]:
                        break
                    assert time.monotonic() < deadline, trajectory
                    time.sleep(0.05)
                assert trajectory["objectId"] == aircraft and trajectory["geographic"] and not trajectory["planar"], trajectory
                track = trajectory["tracks"][trajectory["active"]]["id"]
                call("trajectory_style", {"plot": 0, "track": track, "color": "#ff123456", "width": 4, "name": "试验航迹"})
                styled = call("trajectory_status", {"plot": 0})
                assert styled["name"] == "试验航迹" and styled["width"] == 4 and styled["color"].lower().endswith("123456"), styled
                call("trajectory_style", {"plot": 0, "track": track, "color": "bad-color", "name": "不应提交"}, ok=False)
                assert call("trajectory_status", {"plot": 0})["name"] == "试验航迹"
                call("trajectory_select", {"plot": 0, "track": track})
                call("trajectory_remove", {"plot": 0, "track": "missing"}, ok=False)
                call("object_trajectory", {"object": aircraft, "plot": 1}, wait=True)
                other = call("trajectory_status", {"plot": 1})
                call("trajectory_remove", {"plot": 1, "track": other["tracks"][other["active"]]["id"]})
                assert any(o["id"] == aircraft for o in call("objects"))
                call("view_mode", {"plot": 1, "mode": "curve"})
                call("trajectory_view", {"plot": 0, "preset": 0})
                template = dict(version=1, config={k: v for k, v in config.items() if k != "object"}, roles={
                    "latitude": {"aliases": ["lat"], "unit": "deg"}, "longitude": {"aliases": ["lon"], "unit": "deg"},
                    "height": {"aliases": ["height"], "unit": "m"}})
                assert call("mapping_preview", {"template": template})["ready"]
                template_path = root / "aircraft-template.json"
                call("template_save", {"path": str(template_path), "template": template}, use_file=True)
                assert call("template_read", {"path": str(template_path)}) == template
                before_fields = next(o for o in call("objects") if o["id"] == aircraft)["fields"]
                bad_bindings = {"latitude": sources["lat"], "longitude": 999999}
                assert call("aircraft_setup", {"generation": listing["generation"], "object": aircraft,
                    "config": template["config"], "bindings": bad_bindings}, ok=False)["code"] == "rejected"
                assert next(o for o in call("objects") if o["id"] == aircraft)["fields"] == before_fields
                mapped = call("mapping_apply", {"generation": listing["generation"], "object": aircraft,
                    "template": template, "plot": 0, "cursorTime": 1}, wait=True)
                assert mapped["bindingsReady"] and mapped["missingRoles"] == [] and not mapped["attitudeEnabled"]
                ambiguous = json.loads(json.dumps(template)); ambiguous["roles"]["latitude"]["aliases"] = ["lat", "lon"]
                assert not call("mapping_preview", {"template": ambiguous})["ready"]
                assert call("mapping_apply", {"generation": listing["generation"], "object": aircraft, "template": ambiguous}, ok=False)["code"] == "ambiguous_mapping"
                incompatible = json.loads(json.dumps(template)); incompatible["roles"]["latitude"]["unit"] = "rad"
                assert not call("mapping_preview", {"template": incompatible})["ready"]
                attitude = root / "attitude.csv"
                attitude.write_text("time,roll,pitch,yaw\n0,0,0,0\n1,5,10,30\n2,0,0,45\n", encoding="utf-8")
                call("load", {"path": str(attitude)}, wait=True)
                listing = call("signals")
                sources = {s["originalName"]: s["signal"] for s in listing["signals"]}
                updated = call("aircraft_setup", {"generation": listing["generation"], "object": aircraft,
                    "config": dict(template["config"], attitudeMode=1), "bindings": {
                        "latitude": sources["lat"], "longitude": sources["lon"], "height": sources["height"],
                        "roll": sources["roll"], "pitch": sources["pitch"], "yaw": sources["yaw"]},
                    "plot": 0, "cursorTime": 1}, wait=True)
                assert updated["positionReady"] and updated["attitudeBindingsReady"] and updated["attitudeVisible"], updated
                configured_fields = {f["role"]: f["id"] for f in next(o for o in call("objects") if o["id"] == aircraft)["fields"]}
                assert all(configured_fields[role] == field_id for role, field_id in fields.items())
                fields = configured_fields
                call("capture_window", {"path": str(root / "window.png")}, use_file=True)
                call("wait")
                assert (root / "window.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
                call("save_session", {"path": str(session)}, use_file=True)
                call("restore_session", {"path": str(session)}, use_file=True)
                call("wait")
                restored = next(o for o in call("objects") if o["id"] == aircraft)
                assert {f["role"]: f["id"] for f in restored["fields"]} == fields
                assert call("trajectory_status", {"plot": 0})["objectId"] == aircraft
                call("clear")
                empty = call("status")
                assert empty["fileCount"] == 0 and empty["signalCount"] == 0 and call("objects") == []
                for filename, content in (("part1.csv", "time,A\n0,10\n2,30\n"),
                                          ("part2.csv", "time,A\n1,20\n3,40\n"),
                                          ("conflict.csv", "time,A\n2,99\n")):
                    fragment = root / filename
                    fragment.write_text(content, encoding="utf-8")
                    call("load", {"path": str(fragment)}, use_file=True)
                    call("wait")
                listing = call("signals")
                assert all(s["originalName"] == "A" for s in listing["signals"]), listing
                generation = listing["generation"]
                before = call("status")["signalCount"]
                assert call("merge_signals", {"generation": generation, "groups": [
                    {"name": "valid", "signals": [0, 1]}, {"name": "bad", "signals": [0, 0]}]}, ok=False)["code"] == "rejected"
                assert call("status")["signalCount"] == before and call("objects") == []
                merged = call("merge_signals", {"generation": generation, "groups": [
                    {"name": "merged A", "signals": [0, 1]}, {"name": "conflicting A", "signals": [0, 2]}]})
                assert len(merged["outputs"]) == 2 and not merged["completed"]
                call("wait")
                generation = call("signals")["generation"]
                merged_params = {"generation": generation, "signal": merged["outputs"][0]["signal"]}
                assert call("samples", merged_params)["samples"] == [[0, 10], [1, 20], [2, 30], [3, 40]]
                assert call("statistics", merged_params)["mean"] == 25
                rules = [rule for obj in call("objects") for rule in obj["rules"]]
                assert rules[0]["error"] == "" and "不同值" in rules[1]["error"], rules
                result_page = call("rules", {"state": "failed", "offset": 0, "limit": 1})
                assert result_page["total"] == 1 and result_page["summary"]["failed"] == 1
                assert call("signals", {"logical": True})["total"] == 2
                assert call("logical_view", {"enabled": True})["logicalCount"] == 2
                call("logical_view", {"enabled": False})
                package = root / "logical-package"
                exported = call("export_logical", {"directory": str(package), "format": "mat"}, wait=True)
                assert exported["directory"] == str(package)
                manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
                assert manifest["logicalSignalCount"] == 2 and len(manifest["columns"]) == 2 and len(manifest["objects"]) > 0
                assert manifest["rawSignalCount"] == 3 and len(manifest["sources"]) == 3 and manifest["workspaceSignalCount"] == 5
                assert all(s["sourceFile"] and s["column"] >= 0 for s in manifest["sources"])
                assert (package / "data.mat").stat().st_size > 0
                xlsx_package = root / "logical-xlsx"
                call("export_logical", {"directory": str(xlsx_package), "format": "xlsx"}, wait=True)
                assert zipfile.is_zipfile(xlsx_package / "data.xlsx")
                assert json.loads((xlsx_package / "manifest.json").read_text(encoding="utf-8"))["logicalSignalCount"] == 2
                assert call("export_logical", {"directory": str(package), "format": "mat"}, ok=False, wait=True)["code"] == "operation_failed"
                call("save_session", {"path": str(session)}, use_file=True)
                call("restore_session", {"path": str(session)}, use_file=True)
                call("wait")
                assert any("不同值" in rule["error"] for obj in call("objects") for rule in obj["rules"])
                definitions = call("objects")
                failed_object = next(o for o in definitions if any(r["error"] for r in o["rules"]))
                failed_rule = next(r for r in failed_object["rules"] if r["error"])
                stable_outputs = failed_rule["outputs"]
                source_field = next(f["id"] for f in failed_object["fields"] if f["series"] == 0)
                generation = call("signals")["generation"]
                call("rule_update", {"generation": generation, "object": failed_object["id"], "rule": failed_rule["id"],
                    "config": {"name": "repaired", "operation": "scale", "inputs": [source_field], "factor": 2, "bias": 1}}, wait=True)
                repaired = next(r for o in call("objects") for r in o["rules"] if r["id"] == failed_rule["id"])
                assert repaired["error"] == "" and repaired["outputs"][0]["id"] == stable_outputs[0]["id"] and repaired["outputs"][0]["series"] == stable_outputs[0]["series"]
                generation = call("signals")["generation"]
                assert call("samples", {"generation": generation, "signal": stable_outputs[0]["series"]})["samples"] == [[0, 21], [2, 61]]
                params["generation"] = call("signals")["generation"]
                assert call("layout", {"rows": 0, "columns": 1}, ok=False)["code"] == "invalid_params"
                assert call("statistics", dict(params, generation="old"), ok=False)["code"] == "stale_generation"
                assert call("layout", {"rows": 1, "columns": 1, "typo": True}, ok=False)["code"] == "invalid_params"
                assert call("bogus", ok=False)["code"] == "unknown_command"
                call("statistics", [], ok=False)
                call("clear")
                workbook = root / "fragments.xlsx"
                with zipfile.ZipFile(workbook, "w") as archive:
                    archive.writestr("[Content_Types].xml", '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="xml" ContentType="application/xml"/><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/></Types>')
                    archive.writestr("xl/workbook.xml", '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>' + ''.join(f'<sheet name="part{i}" sheetId="{i}" r:id="rId{i}"/>' for i in range(1, 4)) + '</sheets></workbook>')
                    archive.writestr("xl/_rels/workbook.xml.rels", '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">' + ''.join(f'<Relationship Id="rId{i}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet{i}.xml"/>' for i in range(1, 4)) + '</Relationships>')
                    for i, values in enumerate(([(0, 10), (2, 30)], [(1, 20), (3, 40)], [(2, 99)]), 1):
                        rows = '<row r="1"><c r="A1" t="inlineStr"><is><t>time</t></is></c><c r="B1" t="inlineStr"><is><t>A</t></is></c></row>'
                        rows += ''.join(f'<row r="{j}"><c r="A{j}"><v>{t}</v></c><c r="B{j}"><v>{v}</v></c></row>' for j, (t, v) in enumerate(values, 2))
                        archive.writestr(f"xl/worksheets/sheet{i}.xml", '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>' + rows + '</sheetData></worksheet>')
                call("load", {"path": str(workbook)}, wait=True, use_file=True)
                conflicted = call("merge_preview", {}, wait=True)
                assert not conflicted["ready"] and len(conflicted["conflicts"]) == 1, conflicted
                assert call("merge_execute", {"id": conflicted["id"]}, ok=False)["code"] == "plan_conflicts"
                plan = call("merge_preview", {"policy": {"tables": ["part1", "part2"]}}, wait=True)
                assert plan["ready"] and plan["duplicateGroups"] == 1, plan
                assert call("merge_plan", {"id": plan["id"]})["id"] == plan["id"]
                applied = call("merge_execute", {"id": plan["id"]}, wait=True, request_id="execute-merge-plan")
                assert applied["checkedRules"] == 1 and applied["errors"] == []
                count = call("status")["signalCount"]
                call("merge_execute", {"id": plan["id"]}, wait=True, request_id="execute-merge-plan")
                call("merge_execute", {"id": plan["id"]}, wait=True)
                assert call("status")["signalCount"] == count
                source = applied["outputs"][0]["signal"]
                generation = call("signals")["generation"]
                assert call("samples", {"generation": generation, "signal": source})["samples"] == [[0, 10], [1, 20], [2, 30], [3, 40]]
                existing = call("merge_preview", {"policy": {"tables": ["part1", "part2"]}}, wait=True)
                assert existing["existingGroups"] == 1
                call("rules_retry", {}, wait=True)
                assert call("jobs", {"limit": 10})["total"] >= 10
                large = root / "cancel.csv"
                large.write_text("time,A\n" + "1,2\n" * 1000000, encoding="utf-8")
                count = call("status")["fileCount"]
                pending = call("load", {"path": str(large)}, full=True)
                task = call("job_cancel", {"id": pending["task"]["id"]})
                if task["state"] != "succeeded":
                    cancelled = call("wait", {"job": pending["task"]["id"]}, ok=False, full=True)
                    assert cancelled["task"]["state"] == "cancelled", cancelled
                    assert call("status")["fileCount"] == count
                conn = json.loads(connection.read_text(encoding="utf-8"))
                conn["token"] = "wrong"
                connection.write_text(json.dumps(conn), encoding="utf-8")
                assert call("status", ok=False)["code"] == "unauthorized"
                print("Application + CLI: discovery, analysis, settings, session restore, UTF-8 and errors passed")
            finally:
                app.terminate()
                app.wait(timeout=10)
    return 0


if __name__ == "__main__":
    sys.exit(main())
