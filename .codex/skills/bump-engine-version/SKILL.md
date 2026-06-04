---
name: bump-engine-version
description: Update Solar2D's engine build version in .github/workflows/build.yml. Use when Codex is asked to bump, change, upgrade, tag, or release the Solar2D engine version, either to an explicit version number or by incrementing the current fork build suffix.
---

# Bump Engine Version

Use this skill to update only Solar2D's configured engine version and create the matching release tag.

## Workflow

1. If the user passed a version, use it exactly.
2. If no version was passed, bump the current `FORK_BUILD_NAME` suffix by one letter, for example `2026.3730.k` to `2026.3730.l`.
3. Run the command from the repository root.
4. Report the old and new engine version, the commit hash, the tag, and the pushed branch.

## Command

```bash
zsh bin/AI/bumpEngineVersion.sh [version]
```

The script updates only `.github/workflows/build.yml`, commits only that file, creates a lightweight tag named after the new version, and pushes the committed change and tag.

The commit message and tag name are both:

```text
<version>
```

For local verification without pushing, pass `--no-push`:

```bash
zsh bin/AI/bumpEngineVersion.sh [version] --no-push
```
