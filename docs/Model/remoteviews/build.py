#!/usr/bin/env python3
"""Verify authored excerpts, then assemble the offline RemoteViews explainer."""

import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess


ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
VERIFIER = Path(os.environ.get(
    "SOURCE_PIPELINE_VERIFIER",
    "~/.codex/skills/source-pipeline-explorer/scripts/verify_sources.py",
)).expanduser()


def git(*args):
    return subprocess.check_output(["git", "-C", str(REPO), *args], text=True).strip()


def assemble():
    stages = []
    for source in sorted((ROOT / "data").glob("*stages.json")):
        stages.extend(json.loads(source.read_text())["stages"])
    document = {"stages": stages}
    ids = [stage["id"] for stage in stages]
    if not stages or len(ids) != len(set(ids)):
        raise ValueError("Stage IDs must be nonempty and unique")
    verifier = runpy.run_path(str(VERIFIER))
    report = verifier["verify_data"](REPO, ROOT / "data", pattern="*stages.json",
                                     allowed_roots=[REPO / "base", REPO / "native"])
    (ROOT / "data/source-verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    if not report["ok"]:
        raise ValueError("Source excerpt verification failed; see data/source-verification.json")

    files = sorted({snippet["file"] for stage in stages for snippet in stage["snippets"]})
    source_commit = git("rev-parse", "HEAD")
    base_tree = git("rev-parse", "HEAD:base")
    native_tree = git("rev-parse", "HEAD:native")
    try:
        imported = git("rev-parse", "origin/main")
        if (git("rev-parse", f"{imported}:base") == base_tree
                and git("rev-parse", f"{imported}:native") == native_tree):
            source_commit = imported
    except subprocess.CalledProcessError:
        pass
    document["identity"] = {
        "checkoutCommit": git("rev-parse", "HEAD"),
        "sourceCommit": source_commit,
        "baseTree": base_tree,
        "nativeTree": native_tree,
        "clean": not bool(git("diff", "HEAD", "--name-only", "--", *files)),
        "files": {name: hashlib.sha256((REPO / name).read_bytes()).hexdigest() for name in files},
        "provenance": "Local Git objects and worktree only; no remote permalink verified",
    }
    document["verification"] = {"scope": report["scope"], **report["summary"]}
    document["modelContract"] = {
        "domain": "third-party AppWidget in Launcher, authorized binding, no configuration activity, ordinary XML, no masking or collection conversion",
        "time": "ordinal causal steps, not milliseconds or measured scheduling",
        "visibility": "screen output always unobserved",
        "partial": "only demonstrated setter REPLACE policy; not all RemoteViews actions",
        "async": "selected background executor; callback uses context main Looper",
        "launcher": "one allowed interleaving: listener and workspace before provider update; actual Launcher3 source absent",
        "initialContent": "selected fresh tree requires different initial and update layout IDs; compatible initial content may be reapplied",
        "rendering": "hardware path handoffs only; no GPU, fence, composition or physical display timing",
        "omitted": ["races", "cancellation timing", "collection loading", "notification scheduling", "Launcher3-specific integration", "display measurements"],
    }
    replacements = {
        "/*__STYLE__*/": (ROOT / "src/style.css").read_text(),
        "/*__DATA__*/": json.dumps(document, ensure_ascii=False).replace("<", "\\u003c"),
        "/*__MODEL__*/": (ROOT / "src/model.js").read_text(),
        "/*__APP__*/": (ROOT / "src/app.js").read_text(),
    }
    output = (ROOT / "src/shell.html").read_text()
    for marker, value in replacements.items():
        if output.count(marker) != 1:
            raise ValueError(f"Expected one assembly marker: {marker}")
        output = output.replace(marker, value)
    (ROOT / "index.html").write_text(output)
    (ROOT / "data/source-identity.json").write_text(json.dumps(document["identity"], indent=2) + "\n")
    print(json.dumps({"stages": len(stages), "excerpts": report["summary"],
                      "sourceCommit": source_commit, "output": str(ROOT / "index.html")}))


if __name__ == "__main__":
    assemble()
