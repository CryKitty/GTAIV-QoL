# Canonical source and release procedure

**Canonical branch: main in public CryKitty/GTAIV-QoL.** Existing development
installations continue using successful main builds; the old GTAIV-QoL-dev
archive remains private. See [UPDATER.md](UPDATER.md) for channels and migration.

VERSION now identifies **1.0.0, the first official release**. This preparation PR
creates no tag or release. Only after independent PR verification, merge and a
separate publication instruction, tag the merged clean main head with passing CI.
The existing **v0.1.0-beta.1 prerelease must remain untouched**.

```sh
git fetch origin main
git switch main
git pull --ff-only origin main
git status --short
# Confirm clean tree, VERSION=1.0.0 and passing CI on this exact commit.
git tag -a v1.0.0 -m "GTAIV-QoL 1.0.0"
git push origin v1.0.0
```

Wait for the tag build and both build + updater-ui checks to succeed. The tag
must match VERSION exactly and be reachable from canonical main. The existing
check-beta-tag.py filename is retained, but validation accepts stable semantic
versions and beta versions. Tag payloads retain canonical main identity, with
the exact source SHA and workflow run pinned. CI builds and uploads artifacts;
it does not automatically publish GitHub releases or Nexus pages.

## Assets and full GitHub release

Download both artifacts from the successful tag run:

- **GTAIV-QoL-Nexus-Deck** contains **GTAIV-QoL-1.0.0-Deck.zip**.
- **GTAIV-QoL-public-update** contains **GTAIV-QoL-update.zip**.

These neutral artifact names replace the old beta-specific CI labels for future
builds; historical beta artifacts and release assets are unchanged. Verify the
artifact digests, payload file hashes, version/commit/run identities, and that
the Deck ZIP's nested update ZIP is byte-identical to the standalone asset.

After publication approval, create **GTAIV-QoL 1.0.0** at **v1.0.0** as a FULL
GitHub release, **not a prerelease**, and attach BOTH ZIPs. Draw notes from the
1.0.0 changelog and known-issues summary. Report the tag run/conclusion, release
URL, and both exact asset names, sizes and SHA-256 hashes. Do not replace or edit
the old beta release. Nexus page/upload/publication is a separate action and
must not be inferred from GitHub release approval.

The Deck ZIP contains the guided launcher, exact update payload, README,
changelog, component inventory, known issues, MIT licence, FusionFix notice and
release-info.json. Users extract the whole archive and use the launcher, not a
source ZIP or diagnostic ASI artifact. The launcher verifies the bundled payload
before installing it, including paths with spaces. No manual INI edits are needed.

## Updater and installation contracts

Development checks still select only successful completed non-PR main push or
workflow_dispatch builds from this repository, with matching nonexpired payload
artifacts and digests. Review branches and unrelated tag runs are excluded.
Existing channel and pause-menu choices remain. The public channel (internal ID
`beta`) already accepts full releases and prereleases in version order; full
1.0.0 is supported without changing users' channels. The release-only channel
excludes drafts and prereleases. Both public paths use GTAIV-QoL-update.zip and
require no token; development Actions downloads need read-only authorization.

The guided installer uses its verified bundled version when public checks are
unavailable. Missing dependencies still come from their authors, and Steam
Deck/Linux needs Python 3, KDE or Zenity, an installed Steam Proton tool and the
existing game prefix. This is not a fully offline dependency promise. Existing
settings/backups and the internal Radar-mode restoration journal are preserved.
The FusionFix host remains the verified seed with its own notices and matching
source/build instructions in vendor/fusionfix-menu/README.md.

## Testing scope and known issues

Official 1.0 framing does not turn untested scenarios into accepted ones. Retain
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) in substance, including rare unconfirmed vehicle
auto-shoot, experimental road/police guidance, missing unsupported scene data,
audio limitations and limited Windows gameplay/mod-combination coverage. Turn
Arrows (Beta) is the original Sleeping Dogs-inspired guidance, superseded by
Road Line, and defaults Off on fresh settings; existing saved choices remain.
Use the acceptance checklist and pause-menu controls. Synthetic CPU benchmarks
are not in-game FPS claims. Nexus description remains a draft until its own
publication approval.
