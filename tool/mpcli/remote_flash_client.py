import json
import os
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile


MANIFEST_NAME = "remote_flash_manifest.json"
PATH_OPTIONS = {"-F": "file", "-f": "config", "--bin-dir": "dir"}


def parse_command_line(argv):
    server = None
    mpcli_args = []
    index = 0
    while index < len(argv):
        value = argv[index]
        if value == "--server":
            if index + 1 >= len(argv):
                raise ValueError("--server requires <host>:<port>")
            if server is not None:
                raise ValueError("--server may only be specified once")
            server = argv[index + 1]
            index += 2
            continue
        if value.startswith("--server="):
            if server is not None:
                raise ValueError("--server may only be specified once")
            server = value.split("=", 1)[1]
            index += 1
            continue
        mpcli_args.append(value)
        index += 1
    if not server:
        raise ValueError("the following argument is required: --server")
    return normalize_server(server), mpcli_args


def normalize_server(value):
    server = value.rstrip("/")
    if "://" not in server:
        server = "http://" + server
    parsed = urllib.parse.urlparse(server)
    try:
        port = parsed.port
    except ValueError as exc:
        raise ValueError("--server must be in <host>:<port> form") from exc
    if parsed.scheme not in ("http", "https") or not parsed.hostname or not port:
        raise ValueError("--server must be in <host>:<port> form")
    return server


def _option(token):
    if token in PATH_OPTIONS:
        return token, None
    if token.startswith("--bin-dir="):
        return "--bin-dir", token.split("=", 1)[1]
    if token.startswith("-F") and token != "-F":
        return "-F", token[2:]
    return None, None


def _write_directory(archive, source_dir, archive_dir):
    count = 0
    for root, dirs, files in os.walk(source_dir):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(root, name)
            relative = os.path.relpath(path, source_dir).replace(os.sep, "/")
            archive.write(path, f"{archive_dir}/{relative}")
            count += 1
    if not count:
        raise ValueError(f"directory is empty: {source_dir}")


def make_archive(mpcli_args, output_path):
    rewritten = list(mpcli_args)
    inputs = []
    index = 0
    while index < len(rewritten):
        token = rewritten[index]
        if token == "-c" or token.startswith("-c=") or (token.startswith("-c") and len(token) > 2):
            raise ValueError("-c is selected by the workstation and must not be specified")
        option, inline_value = _option(token)
        if option is None:
            index += 1
            continue
        if option == "-f":
            config_indices = []
            cursor = index + 1
            while cursor < len(rewritten) and not rewritten[cursor].startswith("-"):
                config_indices.append(cursor)
                cursor += 1
            if not config_indices:
                raise ValueError("-f requires a JSON configuration path")
            for config_index in config_indices:
                source = os.path.abspath(rewritten[config_index])
                if not os.path.isfile(source):
                    raise ValueError(f"file does not exist: {source}")
                archive_dir = f"upload/config_{len(inputs)}"
                archive_path = f"{archive_dir}/{os.path.basename(source)}"
                inputs.append(("config", source, archive_dir, archive_path))
                rewritten[config_index] = archive_path
            index = cursor
            continue
        if inline_value is None:
            if index + 1 >= len(rewritten):
                raise ValueError(f"{option} requires a path")
            path_index = index + 1
            path = rewritten[path_index]
        else:
            path_index = index
            path = inline_value
        source = os.path.abspath(path)
        kind = PATH_OPTIONS[option]
        if kind == "dir":
            if not os.path.isdir(source):
                raise ValueError(f"directory does not exist: {source}")
            archive_path = f"upload/dir_{len(inputs)}"
        else:
            if not os.path.isfile(source):
                raise ValueError(f"file does not exist: {source}")
            archive_path = f"upload/file_{len(inputs)}/{os.path.basename(source)}"
        inputs.append((kind, source, archive_path, archive_path))
        rewritten[path_index] = archive_path if inline_value is None else f"{option}={archive_path}"
        index += 2 if inline_value is None else 1

    manifest = {"version": 1, "args": rewritten}
    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as archive:
        archive.writestr(MANIFEST_NAME, json.dumps(manifest, ensure_ascii=True))
        for kind, source, archive_dir, archive_path in inputs:
            if kind in ("dir", "config"):
                directory = source if kind == "dir" else os.path.dirname(source)
                _write_directory(archive, directory, archive_dir)
            else:
                archive.write(source, archive_path)


def request_json(request, timeout=30):
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        try:
            detail = json.load(exc).get("error", str(exc))
        except Exception:
            detail = str(exc)
        raise RuntimeError(detail) from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"cannot connect to server: {exc.reason}") from exc


def print_usage():
    print("usage: remote_flash_client --server <host>:<port> <mpcli arguments>")
    print("example: remote_flash_client --server 127.0.0.1:18790 -T RTL87X3G -M 5 -p -A 0x7009E000 -F app.bin -b 2000000 -r -u -d")
    print("The workstation selects the COM port; do not pass -c.")


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv == ["-h"] or argv == ["--help"]:
        print_usage()
        return 0 if argv else 2
    archive_path = None
    try:
        server, mpcli_args = parse_command_line(argv)
        if not mpcli_args:
            raise ValueError("MPCLI arguments are required")
        fd, archive_path = tempfile.mkstemp(suffix=".zip")
        os.close(fd)
        print("Packaging flash inputs...")
        make_archive(mpcli_args, archive_path)
        size = os.path.getsize(archive_path)
        print(f"Uploading {size / 1024 / 1024:.1f} MiB...")
        with open(archive_path, "rb") as body:
            request = urllib.request.Request(
                server + "/api/tasks", data=body,
                headers={"Content-Type": "application/zip", "Content-Length": str(size)},
                method="POST")
            task = request_json(request, timeout=3600)
        task_id = task["id"]
        print(f"Task {task_id} accepted")
        sequence = 0
        last_state = None
        while True:
            data = request_json(server + f"/api/tasks/{task_id}/events?after={sequence}")
            task = data["task"]
            for event in data["events"]:
                sequence = max(sequence, event["sequence"])
                print(event["message"])
            status = (task["state"], task["stage"], task["queue_position"], task["progress"])
            if status != last_state:
                if task["state"] == "queued":
                    print(f"Queued: position {task['queue_position']}")
                else:
                    print(f"{task['stage']}: {task['progress']:.1f}%")
                last_state = status
            if task["state"] in ("succeeded", "failed"):
                print(task["result"])
                return 0 if task["state"] == "succeeded" else 2
            time.sleep(1)
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"remote_flash_client: error: {exc}", file=sys.stderr)
        return 2
    finally:
        if archive_path:
            try:
                os.remove(archive_path)
            except FileNotFoundError:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
