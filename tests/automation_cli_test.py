"""Real executable + CLI integration, without local datasets or an installed package."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile


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

                def call(command, params=None, ok=True, use_file=False, request_id=None, wait=False, full=False):
                    args = [sys.executable, str(cli), "--connection", str(connection), "--timeout", "15", command]
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
                    result = json.loads(proc.stdout)
                    assert proc.returncode == (0 if ok else 1), (proc.returncode, proc.stdout, proc.stderr)
                    assert result["ok"] == ok, result
                    return result if full else result.get("result") if ok else result["error"]

                status = call("wait")
                assert status["signalCount"] == 1, status
                assert len(call("help")) >= 10
                catalog = call("help")
                assert all(isinstance(c["inputSchema"], dict) and isinstance(c["outputSchema"], dict) for c in catalog)
                schemas = {c["command"]: c for c in catalog}
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
                session = root / "保存.disession"
                call("save_session", {"path": str(session)}, use_file=True)
                call("restore_session", {"path": str(session)}, use_file=True)
                status = call("wait")
                assert status["rows"] == 2 and status["columns"] == 1, status
                assert status["plots"][1]["signals"] == [0], status
                params["generation"] = call("signals")["generation"]
                assert call("nearest", dict(params, time=0.8))["sample"] == [1, 3]
                assert call("samples", dict(params, limit=2))["nextOffset"] == 2
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
