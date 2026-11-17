#!/usr/bin/env python3
"""
Convert absolute workspace paths (e.g. file:///home/ivan/Desktop/betaflight/...)
to portable relative paths in Markdown documentation and logs.

Usage:
    python3 scripts/convert_paths_to_relative.py [targets...]
    python3 scripts/convert_paths_to_relative.py --all-docs
    python3 scripts/convert_paths_to_relative.py --dry-run
"""

import os
import sys
import re
import argparse
from pathlib import Path


# Match file:///home/ivan/Desktop/betaflight/... or /home/ivan/Desktop/betaflight/...
# Stops at markdown URL terminators: whitespace, ), ", ', `, ], >, <
URI_PATTERN = re.compile(
    r"(?:file://)?/home/ivan/Desktop/betaflight/?([^\s\)\"\x27\`\]<>]*)"
)


def resolve_relative_path(repo_root: str, file_dir: str, matched_target: str) -> str:
    """
    Computes the POSIX relative path from file_dir to target_path within repo_root,
    preserving any URL anchor fragment (#...).
    """
    if "#" in matched_target:
        target_path, anchor = matched_target.split("#", 1)
        anchor = "#" + anchor
    else:
        target_path = matched_target
        anchor = ""

    # Target path relative to repository root
    target_abs = os.path.abspath(os.path.join(repo_root, target_path))
    rel_path = os.path.relpath(target_abs, file_dir)

    # Convert Windows backslashes to forward slashes if applicable
    rel_posix = rel_path.replace(os.sep, "/")

    return rel_posix + anchor


def process_file(file_path: str, repo_root: str, dry_run: bool = False) -> int:
    """
    Processes a single file and replaces absolute paths with relative paths.
    Returns the number of replacements made.
    """
    with open(file_path, "r", encoding="utf-8") as fp:
        content = fp.read()

    file_dir = os.path.dirname(os.path.abspath(file_path))

    def replacer(match: re.Match) -> str:
        target = match.group(1)
        return resolve_relative_path(repo_root, file_dir, target)

    new_content, count = URI_PATTERN.subn(replacer, content)

    if count > 0 and not dry_run:
        with open(file_path, "w", encoding="utf-8") as fp:
            fp.write(new_content)

    return count


def collect_markdown_files(targets: list[str]) -> list[str]:
    """
    Collects all .md files under the given target paths (files or directories).
    """
    collected = []
    for target in targets:
        p = Path(target)
        if p.is_file() and p.suffix.lower() == ".md":
            collected.append(str(p.resolve()))
        elif p.is_dir():
            for md_file in p.rglob("*.md"):
                collected.append(str(md_file.resolve()))
    return sorted(set(collected))


def main():
    parser = argparse.ArgumentParser(
        description="Convert absolute workspace paths to relative paths in documentation."
    )
    parser.add_argument(
        "targets",
        nargs="*",
        default=["logs", "knowledgebase"],
        help="Directories or files to process (default: logs knowledgebase)",
    )
    parser.add_argument(
        "--all-docs",
        action="store_true",
        help="Process logs, knowledgebase, PLAN, MACRO_PLAN.md, and DEVELOPMENT_GUIDE.md",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Preview changes without modifying files on disk",
    )

    args = parser.parse_args()
    repo_root = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))

    if args.all_docs:
        targets = [
            "logs",
            "knowledgebase",
            "PLAN",
            "MACRO_PLAN.md",
            "DEVELOPMENT_GUIDE.md",
        ]
    else:
        targets = args.targets

    # Filter targets that actually exist
    existing_targets = [t for t in targets if os.path.exists(os.path.join(repo_root, t))]
    files = collect_markdown_files([os.path.join(repo_root, t) for t in existing_targets])

    print("=" * 70)
    print("Betaflight ARIES v3.0: Absolute Path to Relative Path Normalizer")
    print("=" * 70)
    print(f"Repository Root : {repo_root}")
    print(f"Targets         : {existing_targets}")
    print(f"Total MD Files  : {len(files)}")
    print(f"Mode            : {'DRY-RUN (no disk writes)' if args.dry_run else 'APPLY (modifying files)'}")
    print("-" * 70)

    total_replacements = 0
    modified_files = []

    for fpath in files:
        rel_to_repo = os.path.relpath(fpath, repo_root)
        replacements = process_file(fpath, repo_root, dry_run=args.dry_run)
        if replacements > 0:
            total_replacements += replacements
            modified_files.append((rel_to_repo, replacements))
            print(f"  [MODIFIED] {rel_to_repo:55} ({replacements} replacements)")

    print("-" * 70)
    print(f"Summary: {len(modified_files)} files modified, {total_replacements} total replacements.")
    print("=" * 70)


if __name__ == "__main__":
    main()
