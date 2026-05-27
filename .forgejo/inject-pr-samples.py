#!/usr/bin/env python3
# Copyright (c) 2026 Romain Beauxis <romain.beauxis@gmail.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Inject PR attachment samples into the fate-suite directory.

Usage: inject-pr-samples.py <pr-number>

Reads SAMPLES from the environment (defaults to fate-suite).  For each path
listed in a ```fate-samples``` block in the PR description, downloads the
matching PR attachment into $SAMPLES/<path>.

The PR description should contain a block like:

  ```fate-samples
  vorbis/tos.ogg
  mov/some-new-sample.mov
  ```

Each filename must match a file attached to the PR.
"""

import hashlib
import json
import os
import re
import sys
import tempfile
import urllib.request
from pathlib import Path, PurePosixPath

FORGEJO_API = "https://code.ffmpeg.org/api/v1/repos/ffmpeg/ffmpeg/issues"
ATTACHMENT_BASE = "https://code.ffmpeg.org/attachments/"


def fetch_json(url):
    with urllib.request.urlopen(url) as r:
        return json.load(r)


def parse_fate_samples(body):
    paths = []
    in_block = False
    for line in body.splitlines():
        if line == "```fate-samples":
            in_block = True
        elif line == "```" and in_block:
            break
        elif in_block:
            parts = line.split()
            if len(parts) == 1:
                paths.append(parts[0])
    return paths


MAX_PATH_DEPTH = 3


def validate_path(path):
    p = PurePosixPath(path)
    if p.is_absolute():
        raise ValueError(f"path must be relative: {path!r}")
    if ".." in p.parts:
        raise ValueError(f"path must not contain '..': {path!r}")
    if not p.parts:
        raise ValueError(f"empty path")
    if len(p.parts) > MAX_PATH_DEPTH:
        raise ValueError(f"path too deep (max {MAX_PATH_DEPTH} components): {path!r}")


def validate_url(url):
    if not url.startswith(ATTACHMENT_BASE):
        raise ValueError(f"unexpected attachment URL: {url!r}")


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 16):
            h.update(chunk)
    return h.digest()


def download(url, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=dst.parent, delete=False) as tmp:
        tmp_path = Path(tmp.name)
        try:
            with urllib.request.urlopen(url) as r:
                while chunk := r.read(1 << 16):
                    tmp.write(chunk)
            if dst.exists() and digest(dst) != digest(tmp_path):
                raise ValueError(f"already exists with different content: {dst}")
            tmp_path.rename(dst)
        except:
            tmp_path.unlink(missing_ok=True)
            raise


def main():
    if len(sys.argv) != 2 or not re.fullmatch(r"[0-9]+", sys.argv[1]):
        print(f"Usage: {sys.argv[0]} <pr-number>", file=sys.stderr)
        sys.exit(1)

    pr_number = sys.argv[1]
    samples_dir = Path(os.environ.get("SAMPLES", "fate-suite"))

    pr = fetch_json(f"{FORGEJO_API}/{pr_number}")
    assets = {a["name"]: a["browser_download_url"] for a in pr.get("assets", [])}
    paths = parse_fate_samples(pr.get("body", ""))

    if not paths:
        sys.exit(0)

    new_samples = False

    for path in paths:
        try:
            validate_path(path)
        except ValueError as e:
            print(f"fate-samples: {e}", file=sys.stderr)
            sys.exit(1)

        name = PurePosixPath(path).name
        url = assets.get(name)
        if url is None:
            print(f"fate-samples: no attachment named {name!r}", file=sys.stderr)
            sys.exit(1)

        try:
            validate_url(url)
        except ValueError as e:
            print(f"fate-samples: {e}", file=sys.stderr)
            sys.exit(1)

        dst = samples_dir / path
        is_new = not dst.exists()
        try:
            download(url, dst)
        except ValueError as e:
            print(f"fate-samples: {e}", file=sys.stderr)
            sys.exit(1)
        if is_new:
            new_samples = True
        print(f"Injected: {path}")

    output_file = os.environ.get("FORGEJO_OUTPUT")
    if output_file:
        with open(output_file, "a") as f:
            print(f"new_samples={'true' if new_samples else 'false'}", file=f)


if __name__ == "__main__":
    main()
