"""Offline Unix IPC checks. All storage and logs stay below ignored out/."""

import argparse
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time


class Client:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.reader = self.socket.makefile("rb")
        self.next_id = 0
        self.events = []

    def read(self):
        line = self.reader.readline()
        assert line, "Unexpected IPC EOF"
        return json.loads(line)

    def request(self, method, **fields):
        self.next_id += 1
        request = {"id": self.next_id, "method": method, **fields}
        self.socket.sendall((json.dumps(request) + "\n").encode())
        while True:
            reply = self.read()
            if reply.get("id") == self.next_id and "event" not in reply:
                return reply
            self.events.append(reply)

    def event(self, request_id, event_type, reason=None):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            for index, message in enumerate(self.events):
                if (message.get("id") == request_id
                        and message.get("event") == event_type
                        and (reason is None or message.get("reason") == reason)):
                    return self.events.pop(index)
            self.events.append(self.read())
        raise AssertionError("Event deadline exceeded")

    def close(self):
        self.reader.close()
        self.socket.close()


def launch(executable, endpoint, storage, log, fixture, extra_env=None):
    command = [str(executable)]
    if fixture:
        command.append("--fixture-server")
    process = subprocess.Popen(command + [str(endpoint)], stdout=log, stderr=log,
                               env={**os.environ, "ANIPARSER_DATA_DIR": str(storage), **(extra_env or {})})
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"Server startup failed: {process.returncode}")
        try:
            client = Client(endpoint)
            return process, client
        except OSError:
            time.sleep(0.02)
    process.terminate()
    process.wait(timeout=3)
    raise AssertionError("Listener deadline exceeded")


def stop(process):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise AssertionError("Server failed to stop within 3s")
    assert process.returncode == 0, f"Server exit: {process.returncode}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="out/Debug")
    parser.add_argument("--sanitizers", action="store_true", help="Run instrumented children; disable unsupported LSan under ptrace")
    args = parser.parse_args()
    if args.sanitizers:
        os.environ["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        os.environ["UBSAN_OPTIONS"] = "halt_on_error=1"
    build = Path(args.build).resolve()
    Path("out").mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="ipc-checks-", dir="out")).resolve()
    endpoint = root / "fixture.sock"
    with (root / "server.log").open("w") as log:
        process, client = launch(build / "tests/aniparser_checks", endpoint,
                                 root / "data", log, True)
        try:
            assert client.request("hello")["protocol"] == 3
            reply = client.request("subscribeAnime", anime_id=3)
            assert reply["anime"] is None and reply["refreshing"]
            event = client.event(reply["id"], "anime.updated")
            assert event["ok"] and event["anime"]["anime_id"] == 3
            print("PASS cold Anime snapshot and full update")

            reply = client.request("subscribeSearch", params={"q": "fail"})
            event = client.event(reply["id"], "search.updated")
            assert not event["ok"] and event["status"] == 503
            assert not client.request("unsubscribeSearch", target_id=reply["id"])["unsubscribed"]
            print("PASS terminal Search error clears stream")

            history = client.request("subscribeHistory", limit=1)
            assert history["items"] == []
            assert client.request("addHistory", anime_id=1)["ok"]
            changed = client.event(history["id"], "history.updated", "history")
            metadata = client.event(history["id"], "history.updated", "metadata")
            assert changed["sequence"] < metadata["sequence"]
            assert metadata["items"][0]["anime"]["anime_id"] == 1
            print("PASS collection notifications, metadata, sequence")

            reply = client.request("subscribeAnime", anime_id=1)
            anime = client.event(reply["id"], "anime.updated")["anime"]
            assert anime["players"][0]["dubbers"][0]["episodes"][0]["number"] == "1"
            assert client.request("addPlanned", anime_id=42)["ok"]
            assert client.request("addCompleted", anime_id=42)["ok"]
            assert client.request("getStatus", anime_id=42)["status"] == 3
            assert client.request("updateAnimeSaveData", anime_id=1, player="P",
                                  dubber="D", episode="1", time=100)["ok"]
            assert client.request("removeAnimeSaveData", anime_id=1)["removed"]
            assert client.request("getAnimeSaveData", anime_id=1)["save_data"] is None
            print("PASS status exclusivity and explicit progress deletion")

            for invalid in (-1, 0, 42.9, 2**64 - 1, 1e100, "42", None):
                assert not client.request("addPlanned", anime_id=invalid)["ok"]
            for params in ({"limit": -1}, {"limit": 1.5}, {"offset": -1}):
                assert not client.request("subscribeHistory", **params)["ok"]
            assert not client.request("subscribeSearch", params={"limit": 51})["ok"]
            print("PASS strict numeric validation")

            for _ in range(300):
                transient = Client(endpoint)
                assert transient.request("hello")["ok"]
                transient.close()
            assert client.request("hello")["ok"]
            print("PASS reconnect stress and worker reaping")

            broken = Client(endpoint)
            try:
                broken.socket.sendall(b"{bad json}\n")
                assert not broken.read()["ok"]
                assert broken.request("hello")["ok"]
                # One fragmented request, followed by another complete frame.
                broken.socket.sendall(b'{"id":900,"method":')
                broken.socket.sendall(b'"hello"}\n{"id":901,"method":"hello"}\n')
                assert broken.read()["id"] == 900
                assert broken.read()["id"] == 901
            finally:
                broken.close()
            print("PASS malformed JSON recovery and fragmented JSON Lines")

            boundary = Client(endpoint)
            try:
                first = b'{"id":1000,"method":"hello"}'
                boundary.socket.sendall(first + b" " * (1024 * 1024 - len(first)) + b'\n{"id":1001,"method":"hello"}\n')
                assert boundary.read()["id"] == 1000
                assert boundary.read()["id"] == 1001
            finally:
                boundary.close()
            print("PASS per-line 1 MiB boundary with a following command")

            client.close()
            client = None
            slow = socket.socket(socket.AF_UNIX)
            slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            slow.settimeout(0.2)
            slow.connect(str(endpoint))
            try:
                frame = (json.dumps({"id": "x" * 128, "method": "hello"}) + "\n").encode()
                try:
                    slow.sendall(frame * 20000)
                except (TimeoutError, BrokenPipeError):
                    pass
                started = time.monotonic()
                stop(process)
                assert time.monotonic() - started < 3
                print("PASS stop with a non-reading client")
            finally:
                slow.close()
        finally:
            if client:
                client.close()
            if process.poll() is None:
                stop(process)

        process, client = launch(build / "tests/aniparser_checks", root / "feed-error.sock",
                                 root / "feed-data", log, True, {"ANIPARSER_FIXTURE_FEED_ERROR": "1"})
        try:
            reply = client.request("subscribeFeed")
            event = client.event(reply["id"], "feed.updated")
            assert not event["ok"] and event["status"] == 503 and not event["refreshing"]
            assert not client.request("unsubscribeFeed", target_id=reply["id"])["unsubscribed"]
            print("PASS terminal Feed error clears stream")
        finally:
            client.close()
            stop(process)

        moved_endpoint = root / "moved.sock"
        process, client = launch(build / "tests/aniparser_checks", moved_endpoint,
                                 root / "moved-data", log, True)
        try:
            moved_endpoint.rename(root / "retired.sock")
            moved_endpoint.write_text("preserve this replacement file")
        finally:
            client.close()
            stop(process)
        assert moved_endpoint.read_text() == "preserve this replacement file"
        print("PASS shutdown never unlinks a replacement regular file")

        blocked = root / "not-a-directory"
        blocked.write_text("fixture")
        process, client = launch(build / "src/server/aniparser_server",
                                 root / "storage.sock", blocked, log, False)
        try:
            reply = client.request("updateAnimeSaveData", anime_id=42, player="P",
                                   dubber="D", episode="1", time=10)
            assert not reply["ok"]
            assert client.request("getAnimeSaveData", anime_id=42)["save_data"] is None
            print("PASS real server exposes failed save without changing memory")
        finally:
            client.close()
            stop(process)
    probe = build / "tests/tui_client_checks"
    if probe.exists():
        subprocess.run([str(probe)], check=True, timeout=10)
    print(f"All IPC checks passed; artifacts: {root}")


if __name__ == "__main__":
    main()
