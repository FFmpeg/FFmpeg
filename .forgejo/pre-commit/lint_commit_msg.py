#!/usr/bin/env python3
import json
import os
import re
import subprocess
import sys
import urllib.request as request
from collections.abc import Callable

LintBody = list[str]
LintFunction = Callable[[LintBody], bool]
LintRule = tuple[LintFunction, str]

def call(cmd: list[str]) -> str:
    sys.stdout.flush()
    ret = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, encoding="utf-8", text=True)
    return ret.stdout

lint_rules: dict[str, LintRule] = {}

# A lint rule should return True if everything is okay
def lint_rule(description: str) -> Callable[[LintFunction], None]:
    def f(func: LintFunction) -> None:
        assert func.__name__ not in lint_rules
        lint_rules[func.__name__] = (func, description)
    return f

def get_pr_commits() -> list[tuple[str, list[str]]]:
    pr_number = os.environ["GITHUB_REF"].split("/")[2]
    api_url = os.environ["GITHUB_API_URL"]
    repo = os.environ["GITHUB_REPOSITORY"]

    url = f"{api_url}/repos/{repo}/pulls/{pr_number}/commits"
    req = request.Request(url)
    req.add_header("Accept", "application/vnd.github+json")
    if "GITHUB_TOKEN" in os.environ:
        req.add_header("Authorization", f"token {os.environ['GITHUB_TOKEN']}")

    with request.urlopen(req) as response:
        commits = json.load(response)

    res = []
    for commit in commits:
        sha = commit["sha"]
        message = commit["commit"]["message"]
        res.append((sha, message.splitlines()))

    return res

def get_commits() -> list[tuple[str, list[str]]]:
    if os.environ.get("GITHUB_EVENT_NAME") == "pull_request":
        return get_pr_commits()

    commit_range = sys.argv[1] if len(sys.argv) > 1 else None
    if not commit_range:
        return []

    commits = call(["git", "log", "-z", "--pretty=format:%H%n%B", commit_range]).split("\x00")
    res = []
    for commit in commits:
        if not commit.strip():
            continue
        sha, message = commit.split("\n", 1)
        res.append((sha, message.splitlines()))

    return res

def lint_commit_message(body: list[str]) -> list[str]:
    failed = []
    assert len(body) > 0, "Commit message must not be empty"
    for k, v in lint_rules.items():
        if not v[0](body):
            failed.append(f"* {v[1]} [{k}]")

    return failed

def lint(commits: list[tuple[str, list[str]]]) -> bool:
    print(f"Linting {len(commits)} commit(s):")
    any_failed = False
    for sha, body in commits:
        if failed := lint_commit_message(body):
            any_failed = True
            print("-" * 40)
            if os.environ.get("GITHUB_EVENT_NAME") == "pull_request":
                print(f"Commit {sha[:8]}: {body[0] if body else "(empty)"}")
            else:
                sys.stdout.flush()
                subprocess.run(["git", "-P", "show", "-s", sha])
            print("\nhas the following issues:")
            print("\n".join(failed))
            print("-" * 40)

    return not any_failed


NO_PREFIX_WHITELIST = \
    r"^Revert \"(.*)\"|^Reapply \"(.*)\""

@lint_rule("Subject line must contain a prefix identifying the sub system")
def subsystem_prefix(body: LintBody) -> bool:
    return bool(re.search(NO_PREFIX_WHITELIST, body[0]) or
                re.search(r"^[\w/\.{},-]+: ", body[0]))

@lint_rule("First word after : must be lower case")
def description_lowercase(body: LintBody) -> bool:
    # Allow all caps for acronyms and options with --
    return bool(re.search(NO_PREFIX_WHITELIST, body[0]) or
                re.search(r": (?:[A-Z]{2,} |--[a-z]|[a-z0-9])", body[0]))

@lint_rule("Subject line must not end with a full stop")
def no_dot(body: LintBody) -> bool:
    return not body[0].rstrip().endswith(".")

@lint_rule("There must be an empty line between subject and extended description")
def empty_line(body: LintBody) -> bool:
    return len(body) == 1 or body[1].strip() == ""

@lint_rule("Do not use 'conventional commits' style")
def no_cc(body: LintBody) -> bool:
    return not re.search(r"(?i)^(feat|fix|chore|refactor)[!:(]", body[0])

@lint_rule("Subject line should be shorter than 120 characters")
def line_too_long(body: LintBody) -> bool:
    revert = re.search(r"^Revert \"(.*)\"|^Reapply \"(.*)\"", body[0])
    return bool(revert or len(body[0]) <= 120)

@lint_rule("Prefix should not include file extension")
def no_file_exts(body: LintBody) -> bool:
    return not re.search(r"[a-z0-9]\.([chm]|texi): ", body[0])


if __name__ == "__main__":
    commits = get_commits()
    if not commits:
        print("Usage: ./lint_commits.py <commit-range>")
        exit(1)
    if not lint(commits):
        exit(2)
