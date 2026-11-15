"""Exercise the real Notcurses UI on a PTY against an offline IPC peer."""

import argparse
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import signal
import socket
import struct
import subprocess
import tempfile
import termios
import threading
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="out/Debug")
    parser.add_argument("--sdl-lib", default="")
    parser.add_argument("--sanitizers", action="store_true", help="Use ASan/UBSan with Notcurses-compatible signal-stack settings")
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix="tui-checks-", dir="out")).resolve()
    endpoint = root / "mock.sock"
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(str(endpoint))
    listener.listen(1)
    listener.settimeout(5)
    requests = []
    peer_errors = []
    sender = []

    def anime(anime_id):
        result = {"anime_id": anime_id, "title": f"Fixture {anime_id}",
                  "players": [], "viewing_order": [{"anime_id": 3, "title": "Cold fixture"},
                                    {"anime_id": 4, "title": "Unavailable fixture"}]}
        if anime_id == 1:
            result["players"] = [{"name": "Kodik", "dubbers": [{"name": "Fixture dub",
                "episodes": [{"number": "1", "iframe_url": "fixture"}]}]}]
        return result

    def serve():
        try:
            connection, _ = listener.accept()
            with connection, connection.makefile("rb") as stream:
                lock = threading.Lock()

                def send(message):
                    try:
                        with lock:
                            connection.sendall((json.dumps(message) + "\n").encode())
                    except OSError:
                        pass

                sender.append(send)

                def later(message):
                    timer = threading.Timer(0.1, send, (message,))
                    timer.daemon = True
                    timer.start()

                for line in stream:
                    request = json.loads(line)
                    requests.append(request)
                    reply = {"id": request["id"], "ok": True}
                    method = request["method"]
                    if method == "hello":
                        send({**reply, "protocol": 3})
                    elif method == "subscribeHistory":
                        send({**reply, "revision": 0, "sequence": 0, "items": []})
                    elif method == "subscribeFeed":
                        send({**reply, "refreshing": False, "feed": {"new": [anime(1), anime(2)]}})
                    elif method == "subscribeAnime":
                        send({**reply, "refreshing": True, "loaded": False, "anime": None})
                        if request["anime_id"] == 4:
                            later({**reply, "ok": False, "event": "anime.updated", "anime": None,
                                   "status": 503, "error": "Fixture unavailable"})
                        else:
                            later({**reply, "event": "anime.updated", "anime": anime(request["anime_id"])})
                    elif method == "subscribeSearch":
                        send({**reply, "refreshing": True, "offset": 0, "limit": 10, "results": []})
                        later({**reply, "event": "search.updated", "offset": 0, "limit": 10, "results": [anime(2)]})
                    elif method == "getVideoOptions":
                        send({**reply, "qualities": [{"quality": "720", "url": "https://fixture.invalid/" + "long/" * 30 + "stream.m3u8"},
                                                       {"quality": "480", "url": "https://fixture.invalid/480.m3u8"}]})
                    else:
                        send({**reply, "ok": False, "error": "Unsupported fixture method"})
        except Exception as error:
            peer_errors.append(str(error))

    worker = threading.Thread(target=serve, daemon=True)
    worker.start()
    master, slave = pty.openpty()
    dimensions = [30, 100]
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", *dimensions, 480, 800))
    env = {**os.environ, "TERM": "xterm-256color", "LANG": "C.UTF-8"}
    if args.sanitizers:
        # Notcurses installs its own alternate signal stack. Let it own that
        # stack instead of having libasan attempt munmap on its heap buffer.
        env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1:use_sigaltstack=0"
        env["UBSAN_OPTIONS"] = "halt_on_error=1"
    if args.sdl_lib:
        env["LD_LIBRARY_PATH"] = args.sdl_lib + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    process = subprocess.Popen([str(Path(args.build).resolve() / "tui/aniparser_tui"), str(endpoint)],
                               stdin=slave, stdout=slave, stderr=slave, env=env)
    os.close(slave)
    transcript = bytearray()
    query_buffer = b""

    def pump(seconds):
        nonlocal query_buffer
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if not select.select([master], [], [], 0.03)[0]:
                continue
            try:
                data = os.read(master, 65536)
            except OSError:
                break
            if not data:
                break
            transcript.extend(data)
            query_buffer += data
            # Answer the terminal capability queries Notcurses needs on a raw
            # PTY. This is a terminal fixture, not an emulation of its rendering.
            for query, response in {
                b"\x1b[6n": b"\x1b[1;1R", b"\x1b[c": b"\x1b[?1;2c",
                b"\x1b[>c": b"\x1b[>0;379;0c", b"\x1b[>0c": b"\x1b[>0;379;0c",
                b"\x1b[14t": b"\x1b[4;480;800t", b"\x1b[16t": b"\x1b[6;16;8t",
                b"\x1b[18t": f"\x1b[8;{dimensions[0]};{dimensions[1]}t".encode(),
                b"\x1b]10;?\x1b\\": b"\x1b]10;rgb:ffff/ffff/ffff\x1b\\",
                b"\x1b]11;?\x1b\\": b"\x1b]11;rgb:0000/0000/0000\x1b\\",
                b"\x1b[?2026$p": b"\x1b[?2026;2$y",
            }.items():
                while query in query_buffer:
                    os.write(master, response)
                    query_buffer = query_buffer.replace(query, b"", 1)
            query_buffer = query_buffer[-256:]

    def key(value, pause=0.25):
        os.write(master, value.encode() if isinstance(value, str) else value)
        pump(pause)
        assert process.poll() is None, f"TUI exited early: {process.returncode}"

    try:
        pump(2)
        assert process.poll() is None, "TUI initialization failed"
        assert any(request["method"] == "subscribeFeed" for request in requests), "TUI never loaded feed"
        key("\r", 0.4)       # cold Anime 1
        key("\r")            # players
        key("\r")            # dubbers
        key("\r")            # episodes
        key("\r")            # direct links via fixture
        key(b"\x1b[B")         # second quality
        for _ in range(4):
            key(b"\x1b")       # details
        key("v")
        key("\r", 0.4)       # cold Anime 3 from viewing order
        key(b"\x1b")
        key(b"\x1b[B")
        key("\r", 0.4)       # cold Anime 4 terminates with an error
        assert b"Fixture unavailable" in transcript, "Cold Anime error was not displayed"
        key(b"\x1b")
        key(b"\x1b")
        key("\r")            # players of restored Anime 1
        key("/")
        key("я")
        key(b"\x7f")
        key("fixture\r", 0.4)
        key("\r", 0.4)       # Anime 2, empty players
        key(b"\x1b")          # search
        key(b"\x1b")          # restores Anime 1 + its players
        key("\r")            # safe dubbers
        dimensions[:] = [18, 60]
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", *dimensions, 288, 480))
        process.send_signal(signal.SIGWINCH)
        pump(0.3)
        searches = [r["params"]["q"] for r in requests if r["method"] == "subscribeSearch"]
        assert searches == ["fixture"], f"Backspace corrupted query: {searches}"
        assert any(r["method"] == "getVideoOptions" for r in requests)
        assert b"No supported" not in transcript, "Navigation restored a different Anime model"
        original_request = next(r for r in reversed(requests) if r["method"] == "subscribeAnime" and r["anime_id"] == 1)
        replacement = anime(1)
        replacement["players"] = []
        before_replacement = len(transcript)
        sender[0]({"id": original_request["id"], "ok": True, "event": "anime.updated", "anime": replacement})
        pump(0.3)
        key("\r")
        assert b"No supported" in transcript[before_replacement:], "Full update retained removed players"
        os.write(master, b"q")
        pump(0.3)
        process.wait(timeout=3)
        assert process.returncode == 0, f"TUI exit: {process.returncode}"
        assert not peer_errors, peer_errors
        print("PASS PTY TUI: cold loading/error, nested menus, links, UTF-8, return from search/viewing order, empty full update, resize, q")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)
        (root / "terminal.log").write_bytes(transcript)
        (root / "requests.json").write_text(json.dumps(requests, ensure_ascii=False, indent=2))
        # A searchable transcript accompanies the original terminal byte stream.
        text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", transcript.decode(errors="replace"))
        (root / "terminal.txt").write_text(text)
        os.close(master)
        listener.close()
        worker.join(timeout=1)
        print(f"TUI artifacts: {root}")


if __name__ == "__main__":
    main()
