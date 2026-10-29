"""Check a diagnostics file against the format documented in asmdiag.h:
a results report (nasm 1.3, --report=2) or a bare diagnostics array (1.2).

Usage: python3 -I check_json.py <file>...

Stricter than nStudio's own parser, which tolerates what it does not know:
this is what nasm promises to write, so anything else is a bug on its side.
Exits 1 with one line per problem.
"""

import json
import sys

SEVERITIES = {"error", "warning", "note"}
RUN_STATUSES = {"assembled", "errors", "refused", "cancelled"}
FORMATS = {"zehn", "prg", ""}


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


def check_report(path, doc, problems):
    """A results report (nasm --report=2): the run, then the diagnostics."""
    fields = {
        "version": int, "status": str, "output": str, "format": str,
        "code_size": int, "errors": int, "warnings": int, "diagnostics": list,
    }
    for key, kind in fields.items():
        if not isinstance(doc.get(key), kind) or isinstance(doc.get(key), bool):
            problems.append(f"{path}: '{key}' missing or not {kind.__name__}")
            return
    if doc["version"] != 2:
        problems.append(f"{path}: version {doc['version']}, expected 2")
    if doc["status"] not in RUN_STATUSES:
        problems.append(f"{path}: unknown status {doc['status']!r}")
    if doc["format"] not in FORMATS:
        problems.append(f"{path}: unknown format {doc['format']!r}")
    assembled = doc["status"] == "assembled"
    if doc["code_size"] < 0 or (not assembled and doc["code_size"] != 0):
        problems.append(f"{path}: code_size {doc['code_size']} for a run "
                        f"that is {doc['status']}")
    if assembled and (not doc["output"] or not doc["format"]):
        problems.append(f"{path}: assembled, but no output or format")
    if doc["status"] in ("refused", "cancelled") and doc["output"]:
        problems.append(f"{path}: {doc['status']}, yet an output")
    diags = doc["diagnostics"]
    for i, d in enumerate(diags):
        check_diagnostic(f"{path}.diagnostics[{i}]", d, problems)
    # the counts cover every diagnostic, so never fewer than those listed
    for sev, key in (("error", "errors"), ("warning", "warnings")):
        listed = sum(1 for d in diags
                     if isinstance(d, dict) and d.get("severity") == sev)
        if doc[key] < listed:
            problems.append(f"{path}: {key} {doc[key]}, but {listed} listed")
    if doc["status"] == "errors" and doc["errors"] == 0:
        problems.append(f"{path}: errors, but none counted")


def check_file(path):
    problems = []
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as e:
        return [f"{path}: {e}"]
    if isinstance(doc, dict):
        check_report(path, doc, problems)
        return problems
    if not isinstance(doc, list):
        return [f"{path}: neither a results report nor a JSON array"]
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
