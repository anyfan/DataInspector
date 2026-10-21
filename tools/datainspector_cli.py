#!/usr/bin/env python3
"""Control an opt-in DataInspector instance. Python 3.9+, standard library only."""
import argparse
import json
import math
import socket
import sys
import time
from pathlib import Path

MAX_RESPONSE = 4 * 1024 * 1024


class ClientError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def request(connection, command, params, timeout, request_id=None):
    if type(connection.get("protocol")) is not int or connection.get("protocol") != 1:
        raise ClientError("protocol_mismatch", "Expected wire protocol 1; connection advertises "
                          + repr(connection.get("protocol")) + ". Use a matching CLI and DataInspector instance.")
    if connection.get("host") != "127.0.0.1":
        raise ValueError("Unsupported connection host")
    port = connection.get("port")
    token = connection.get("token")
    if type(port) is not int or not 1 <= port <= 65535 or not isinstance(token, str) or not token:
        raise ValueError("Invalid connection descriptor")
    message = {"token": token, "command": command, "params": params}
    if request_id is not None:
        message["requestId"] = request_id
    payload = json.dumps(message,
                         ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8") + b"\n"
    if len(payload) > 65536:
        raise ValueError("Request exceeds 64 KiB")
    deadline = time.monotonic() + timeout
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Connection deadline exceeded")
        sock.settimeout(remaining)
        sock.sendall(payload)
        data = bytearray()
        while b"\n" not in data:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Response deadline exceeded")
            sock.settimeout(remaining)
            chunk = sock.recv(min(65536, MAX_RESPONSE + 1 - len(data)))
            if not chunk:
                raise ConnectionError("Connection closed without a complete response")
            data.extend(chunk)
            if len(data) > MAX_RESPONSE:
                raise ValueError("Response exceeds 4 MiB")
    result = json.loads(data.split(b"\n", 1)[0])
    if not isinstance(result, dict) or type(result.get("ok")) is not bool:
        raise ValueError("Invalid response")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--connection", required=True, type=Path,
                        help="JSON file created by DataInspector --automation-file")
    parser.add_argument("--timeout", type=float, default=60, help="Total timeout in seconds")
    parser.add_argument("command", help="help, status, signals, samples, statistics, nearest, layout, etc.; wait polls until idle")
    parser.add_argument("--request-id", help="Idempotency key; reuse with identical command and parameters when retrying")
    parser.add_argument("--wait", action="store_true", help="Wait for the returned task and report its actual success/failure")
    parser.add_argument("--ascii", action="store_true", help="Escape non-ASCII text in JSON for encoding-independent shell pipelines")
    parser.add_argument("--expect-api-version", type=int, help="Check server API revision before sending the command")
    parser.add_argument("--expect-catalog-hash", help="Require the SHA256 command schema fingerprint discovered from help/capabilities")
    params = parser.add_mutually_exclusive_group()
    params.add_argument("--params", default="{}", help="JSON parameter object")
    params.add_argument("--params-file", type=Path, help="UTF-8 JSON file (avoids shell quoting)")
    args = parser.parse_args()
    task = None
    try:
        if not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError("timeout must be positive and finite")
        connection = json.loads(args.connection.read_text(encoding="utf-8-sig"))
        if not isinstance(connection, dict):
            raise ValueError("Connection descriptor must be an object")
        raw = args.params_file.read_text(encoding="utf-8-sig") if args.params_file else args.params
        values = json.loads(raw)
        if not isinstance(values, dict):
            raise ValueError("params must be a JSON object")
        deadline = time.monotonic() + args.timeout
        if args.expect_api_version is not None and args.expect_api_version < 1:
            raise ValueError("expect-api-version must be positive")
        if args.expect_catalog_hash is not None and (len(args.expect_catalog_hash) != 64
                or any(c not in "0123456789abcdef" for c in args.expect_catalog_hash.lower())):
            raise ValueError("expect-catalog-hash must be a SHA256 hex string")
        if args.expect_api_version is not None or args.expect_catalog_hash is not None:
            discovery = request(connection, "capabilities", {}, args.timeout)
            if not discovery["ok"]:
                error = discovery.get("error", {})
                if error.get("code") == "unknown_command":
                    raise ClientError("version_mismatch", "Server cannot report API capabilities. Read help on this connection; "
                                      "restart the updated DataInspector or use a matching CLI. Command was not sent.")
                raise ClientError(error.get("code", "capability_check_failed"), "Capability check failed: "
                                  + error.get("message", "Unknown server error") + ". Command was not sent.")
            capabilities = discovery["result"]
            if not isinstance(capabilities, dict):
                raise ValueError("Invalid capabilities response; command was not sent")
            if capabilities.get("protocolVersion") != 1:
                raise ClientError("protocol_mismatch", "Server capabilities do not advertise wire protocol 1. Command was not sent.")
            if args.expect_api_version is not None and capabilities.get("apiVersion") != args.expect_api_version:
                raise ClientError("version_mismatch", "Expected API " + str(args.expect_api_version) + ", server reports "
                                  + str(capabilities.get("apiVersion")) + ". Read help for this connection. Command was not sent.")
            if args.expect_catalog_hash is not None and capabilities.get("catalogHash") != args.expect_catalog_hash.lower():
                raise ClientError("version_mismatch", "Command catalog differs from the expected SHA256. Read help for this connection. Command was not sent.")
        if args.command == "wait":
            if values and (set(values) != {"job"} or not isinstance(values["job"], str)):
                raise ValueError("wait accepts only an optional job task ID")
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Timed out waiting for DataInspector to become idle")
                if values:
                    result = request(connection, "job_status", {"id": values["job"]}, remaining)
                    if not result["ok"]:
                        break
                    task = result["result"]
                    if task["state"] in ("succeeded", "failed", "cancelled"):
                        result = {"ok": task["state"] == "succeeded", "task": task, "result": task.get("result", {})}
                        if not result["ok"]:
                            result["error"] = task.get("error", {"code": task["state"], "message": "Task did not succeed"})
                        break
                    time.sleep(min(0.1, max(0, deadline - time.monotonic())))
                    continue
                result = request(connection, "status", {}, remaining)
                if not result["ok"] or not any(result["result"][key] for key in
                                               ("loading", "restoring", "deriving", "exporting", "analyzing", "rendering")):
                    break
                time.sleep(min(0.1, max(0, deadline - time.monotonic())))
        else:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Version check consumed the request deadline; command was not sent")
            result = request(connection, args.command, values, remaining, args.request_id)
            if not result["ok"] and result.get("error", {}).get("code") == "unknown_command":
                result["error"]["message"] += "; read help on this connection, and restart DataInspector if this is an older running instance"
            if args.wait and result["ok"] and result.get("task"):
                task = result["task"]
                while task["state"] not in ("succeeded", "failed", "cancelled"):
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError("Task wait timed out; use job_status with the returned task ID")
                    value = request(connection, "job_status", {"id": task["id"]}, remaining)
                    if not value["ok"]:
                        result = value
                        break
                    task = value["result"]
                    if task["state"] in ("succeeded", "failed", "cancelled"):
                        break
                    time.sleep(min(0.1, max(0, deadline - time.monotonic())))
                if result["ok"]:
                    result = {"ok": task["state"] == "succeeded", "task": task, "result": task.get("result", {})}
                    if not result["ok"]:
                        result["error"] = task.get("error", {"code": task["state"], "message": "Task did not succeed"})
    except ClientError as error:
        result = {"ok": False, "error": {"code": error.code, "message": str(error)}}
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {"ok": False, "error": {"code": "client_error", "message": str(error)}}
        if task is not None:
            result["task"] = task
    print(json.dumps(result, ensure_ascii=args.ascii, allow_nan=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
