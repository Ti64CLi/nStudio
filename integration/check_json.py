"""Check a diagnostics file against the format documented in asmdiag.h.

Usage: python3 -I check_json.py <file>...

Stricter than nStudio's own parser, which tolerates what it does not know:
this is what nasm promises to write, so anything else is a bug on its side.
Exits 1 with one line per problem.
"""

import json
import sys

SEVERITIES = {"error", "warning", "note"}


def check_location(where, obj, keys, problems):
    """Check a related location or an include frame."""
    if not isinstance(obj, dict):
        problems.append(f"{where}: not an object")
        return
    for key, kind in keys.items():
        if not isinstance(obj.get(key), kind):
            problems.append(f"{where}: '{key}' missing or not {kind.__name__}")


def check_diagnostic(where, d, problems):
    if not isinstance(d, dict):
        problems.append(f"{where}: not an object")
        return
    fields = {
        "severity": str, "code": str, "file": str, "line": int, "col": int,
        "end_col": int, "message": str, "source_line": str,
        "expanded_from": str, "related": list, "include_stack": list,
    }
    for key, kind in fields.items():
        if not isinstance(d.get(key), kind) or isinstance(d.get(key), bool):
            problems.append(f"{where}: '{key}' missing or not {kind.__name__}")
            return
    if d["severity"] not in SEVERITIES:
        problems.append(f"{where}: unknown severity {d['severity']!r}")
    if d["line"] != -1 and d["line"] < 1:
        problems.append(f"{where}: line {d['line']} is neither 1-based nor -1")
    if d["col"] < 0 or d["end_col"] < 0:
        problems.append(f"{where}: negative column")
    if d["col"] and d["end_col"] and d["end_col"] < d["col"]:
        problems.append(f"{where}: column span ends before it starts")
    for i, r in enumerate(d["related"]):
        check_location(f"{where}.related[{i}]", r,
                       {"file": str, "line": int, "col": int, "end_col": int,
                        "message": str}, problems)
    for i, fr in enumerate(d["include_stack"]):
        check_location(f"{where}.include_stack[{i}]", fr,
                       {"file": str, "line": int}, problems)
        if isinstance(fr, dict) and isinstance(fr.get("line"), int) \
                and fr["line"] < 1:
            problems.append(f"{where}.include_stack[{i}]: line {fr['line']}")


def check_file(path):
    problems = []
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as e:
        return [f"{path}: {e}"]
    if not isinstance(doc, list):
        return [f"{path}: not a JSON array"]
    for i, d in enumerate(doc):
        check_diagnostic(f"{path}[{i}]", d, problems)
    return problems


def main():
    problems = []
    for path in sys.argv[1:]:
        problems += check_file(path)
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
