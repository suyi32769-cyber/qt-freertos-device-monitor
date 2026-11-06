"""Exercise the actual FreeRTOS process and its localhost transport."""
import pathlib
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parent

def frame(body):
    checksum = 0
    for b in body.encode("ascii"):
        checksum ^= b
    return f"${body}*{checksum:02X}\n".encode("ascii")

def receive_until(sock, predicate, timeout=12):
    end = time.monotonic() + timeout
    lines, pending = [], b""
    while time.monotonic() < end:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if not data:
            raise AssertionError("connection closed")
        pending += data
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            assert line.startswith(b"$") and b"*" in line, line
            body, check = line[1:].rsplit(b"*", 1)
            assert frame(body.decode()).strip().endswith(b"*" + check), line
            lines.append(body.decode())
            if predicate(lines):
                return lines
    raise AssertionError(f"timeout: {lines}")

def main():
    old_log = (ROOT / "controller.log").stat().st_size if (ROOT / "controller.log").exists() else 0
    proc = subprocess.Popen([str(ROOT / "bin/device_controller.exe")], cwd=ROOT,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 8
        while True:
            if proc.poll() is not None:
                raise RuntimeError(proc.stderr.read().decode(errors="replace"))
            try:
                sock = socket.create_connection(("127.0.0.1", 47653), timeout=1)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.1)
        with sock:
            sock.settimeout(.5)
            first = receive_until(sock, lambda a: any(x.startswith("DATA,") for x in a) and "STATE,RUNNING" in a)
            sock.sendall(b"$CMD,FAULT*00\n")
            after_bad = receive_until(sock, lambda a: sum(x.startswith("DATA,") for x in a) >= 5)
            assert "ACK,FAULT" not in after_bad and "STATE,FAULT" not in after_bad, after_bad
            sock.sendall(frame("CMD,FAULT")[:5]); sock.sendall(frame("CMD,FAULT")[5:])
            failure = receive_until(sock, lambda a: "ACK,FAULT" in a and "STATE,FAULT" in a)
            sock.sendall(frame("CMD,RESET"))
            recovery = receive_until(sock, lambda a: "ACK,RESET" in a and "STATE,RECOVERING" in a and "STATE,RUNNING" in a)
            print("PASS: checksummed data, fragmented command, fault timeout, reset, recovery", len(first), len(failure), len(recovery))
        log = (ROOT / "controller.log").read_bytes()[old_log:].decode(errors="replace")
        assert "Invalid command or checksum" in log
        assert "CMD,FAULT" in log and "STATE,FAULT" in log and "STATE,RECOVERING" in log
    finally:
        proc.terminate()
        proc.wait(timeout=5)

if __name__ == "__main__":
    main()
