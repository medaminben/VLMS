# Contributing

Issues and pull requests are welcome. Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).

## Before you open a pull request

1. Build and run the tests as described in the [README](README.md#tests).
2. Keep changes focused. A pull request should do one thing.
3. Do not add real member records, identity documents, or a live catalogue. The files under `database/` and `resources/` are sample data. The images under `resources/` are not tracked; to change them, see [Sample data](docs/windows-installer.md#sample-data).

## Branches

Work on a feature branch and open the pull request against `Beta`. `main` and `Beta` only accept changes through pull requests. The maintainer merges `Beta` into `main` when a release is due.

## Versioning and commits

Releases are automatic. A merge into `main` makes `scripts/release/release.py` choose the version from the diff since the last `vX.Y.Z` tag, tag it, and publish the release. So:

- Do not create or push `v*` tags, and do not change version numbers by hand.
- Each commit subject becomes a line in the release notes and the changelog. Write one sentence in the imperative that says what changed, starting with a capital and ending with a full stop, for example `Fix the due date shown after a renewal.` Leave out prefixes like `feat:` and work-in-progress subjects like `wip` or `fix typo`.
- Before you open the pull request, squash or reword commits that would read badly in the release notes.

## Licence

By contributing, you agree that your contribution is licensed under the [MIT Licence](LICENSE) that covers this repository.
