# Linux Patch Series

The [release configuration](../../config/release.json) selects the product kernel and patch series. The current series' [`series.json`](7.3/series.json) records the public source baseline, patch order, commits and parent chain, SHA256 of each patch, and the resulting source tree after application.

Split patches following Linux mainline conventions so each entry makes a single independently reviewable change. When fixing newly added code that has not yet gone upstream, amend the patch that introduced it, replay subsequent patches, and update the manifest; fixes for existing upstream issues belong in separate patches. When migrating kernel versions, drop patches already superseded by upstream and do not duplicate the same fix per distribution.

Run `./build.sh linux` in the [build environment](../../docs/devel/building.md). The build entry point prepares the baseline from a pinned public source, applies patches sequentially in `build/kernel-worktrees/`, and verifies the source tree at each step. Fetching original patch commit objects from a remote is not required. The application record is written to `build/release/source-manifest.json`, recording the actual commits used for subsequent builds. Re-running verifies the existing worktree; if application fails, the worktree is preserved for troubleshooting.
