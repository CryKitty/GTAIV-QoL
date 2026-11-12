# Canonical source and beta release procedure

**Canonical branch: main in the new public CryKitty/GTAIV-QoL.** There is no scaffold main or legacy feature-branch release source here. The renamed GTAIV-QoL-dev archive stays private with its history intact. Existing Deck installations/automations must follow the migration steps in [UPDATER.md](UPDATER.md#migrate-an-existing-deck-install).

VERSION identifies the proposed beta (0.1.0-beta.1). A release tag must be v plus that exact value, on a clean main commit with passing CI. The commands below are maintainer instructions for a separately approved publication, not actions performed during the repository migration. **No beta release or tag is published yet.**

```sh
git fetch origin main
git switch main
git pull --ff-only origin main
git status --short
# Confirm the tree is clean, VERSION is correct, and this commit's CI passed.
git tag -a v0.1.0-beta.1 -m "GTAIV-QoL 0.1.0-beta.1"
git push origin v0.1.0-beta.1
```

These are maintainer instructions, not actions performed by this PR. No tag, GitHub release, Nexus page or environment is published automatically. Choose the next beta number in VERSION/changelog before subsequent tags.

## Tag and updater contracts

`build.yml` still builds pushes to the existing canonical branch. It also accepts beta tags, requires the tag to match VERSION, and checks that the tagged commit is reachable from the central UPDATE_SOURCE canonical branch. The normal MSVC build and all tests run before packaging. Tag builds use canonical branch identity in the payload manifest; source SHA and workflow run remain pinned.

The development updater selects main in this new repository. It selects only completed successful **push/workflow_dispatch** builds from the exact repository/branch with nonexpired matching payload artifacts and valid digests. PR builds, forks, tag runs on other head branches, failures, in-progress runs and wrong/missing payloads are excluded. The selector's regression tests verify those cases. A review-branch artifact is for manual maintainer QA, not something the installed updater will silently select.

The release-only channel remains separate: it excludes drafts and prereleases, so beta tags/Nexus uploads do not silently move development users into release mode. It requires a published accessible full-release asset named GTAIV-QoL-update.zip. Public beta downloads need no token; Actions artifact downloads for development require read-only authorization. No release is published by the migration.

## Downloadable beta artifacts

After a successful canonical/tag build, download CI artifact **GTAIV-QoL-Nexus-beta-Deck**. It contains one ready-to-upload archive, **GTAIV-QoL-0.1.0-beta.1-Deck.zip**. Upload that inner archive to Nexus after review/acceptance, rather than the diagnostic ASI artifact or a source-code ZIP.

The archive contains a guided launcher, GTAIV-QoL-update.zip, README, current changelog, seven-component inventory, known issues, MIT LICENSE, existing FusionFix notice and release-info.json identifying the version, commit, run and payload hash. The inner payload includes both ASIs and the helper embedding the verified FusionFix menu host. The launcher verifies the payload hash and exact commit/branch/run/file identities before the guide installs it. Its command stays short even with a large bundle and supports paths with spaces.

First-time Nexus installation uses the bundled beta, not a private CI download. The existing dependency guide still discovers Steam/SD-card game installs and obtains missing dependencies from their authors. No user's credentials are included. Author downloads and an installed Steam Proton tool/existing game prefix mean this is **repository-independent installation**, not a promise of fully offline dependency setup.

A Nexus install includes the public beta updater and retains menu settings. After separate publication approval, attach the matching GTAIV-QoL-update.zip from CI artifact GTAIV-QoL-public-beta-update to the public GitHub beta release. CI only builds/uploads artifacts; it does not publish releases or Nexus pages automatically. The workflow restores the verified FusionFix menu seed from this public tree, with no private Actions access; its matching source and build instructions are in vendor/fusionfix-menu/README.md.

## Release acceptance

Use [KNOWN_ISSUES.md](KNOWN_ISSUES.md) to check all seven components, especially the previously unconfirmed road-line recovery. Keep beta limitations and rare unconfirmed auto-shoot in the page/changelog. [NEXUS_DESCRIPTION.md](NEXUS_DESCRIPTION.md) is a draft, not a published page. CPU profiling evidence is in [PERFORMANCE.md](PERFORMANCE.md); do not turn those numbers into FPS claims.
