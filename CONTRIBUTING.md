# Contributing

Use Conventional Commits: `feat(vdec): ...`, `fix(runtime): ...`,
`docs: ...`, `test: ...`, `build: ...`, `ci: ...`. Keep hardware changes,
build changes and documentation reviewable as separate commits.

Run `make test` before submitting. It fetches pinned firmware sources,
runs Python tests, C ASan/UBSan checks and documentation links. Run
`bash scripts/build-kernel.sh` for driver changes, and regenerate the
additive patch with `scripts/export-kernel-patch.py` on the clean matching
baseline when codec sources change. CI rejects stale patch content.

[Build instructions](docs/build.md) list cross-toolchain dependencies.
[CI and release instructions](docs/release.md) describe required jobs and
artifact provenance. Keep the English and Chinese README sections equivalent.
Documentation links should be relative so GitHub and release source archives
can resolve them.

Follow the [repository content policy](docs/repository-policy.md). Do not commit
`evidence/`, `private/`, run logs, generated reports, output media or deployment
drafts. Store them in ignored `build/reports/` and archive useful originals
outside the checkout. Promote reusable checks to `tests/` and retain concise,
version-specific conclusions in `docs/`; do not move run transcripts there.
`make test` rejects tracked retired/generated directories and logs, and local
documentation links to them. Do not bypass this rule with `git add -f`.
Before publishing, remove real device IP/MAC/Tailnet names, credentials, tokens
and machine-specific backup paths. Keep only generic operator examples and
reserved test addresses; the full scan report stays in ignored `build/reports/`.

Driver changes require a separate hardware report: kernel, source commit,
module hashes, stream metadata, complete frame count/content, error recovery,
services and rollback. Record failures and limits; successful build/exit codes
are not image correctness. Keep source/module identities and limits in the
[validation records](docs/validation-records.md); full reports belong to a
verified external archive, not Git. Historical results are snapshots, not
current claims. Do not change `/boot` or assume another board's phandles.
