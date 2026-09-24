<!--
The fleet README skeleton. scripts/generate-readme.mjs renders the two sections
marked "generated", and the config block inside Configuration, and leaves
everything else to the repo.

A generated section is owned by core. Editing one in a mod repo is drift that
`node scripts/generate-readme.mjs --all` reports and `--write` reverts, so a
correction belongs in the generator, not in the copy.

Every other section is owned by the repo, and the generator will not invent one.
A claim about a specific game, engine, publisher or bug has to be written by
whoever verified it. That is the whole reason the generated set is two sections
and not twelve: an OpenTrack setup paragraph is the same everywhere, and an
Installation section is not.

The generator reads a shipped HeadTracking.ini, for the UDP port the mod
actually listens on, and for the config block, data/config-format.json and the
repo's committed canonical config. It deliberately does not read lopari's
catalog. That was tried for the Controls table, and 46 of the catalog's 54
entries advertise a Recenter hotkey on Home / Ctrl+Shift+T that no mod in the
fleet binds.
-->

# {{Game}} Head Tracking

Intro line: what the mod does, and that it is driven by OpenTrack over UDP with
no VR headset required. Do not describe what other people's trackers do.

## Features                                        <!-- hand-written -->

## Requirements                                    <!-- hand-written -->

## Installation                                    <!-- hand-written -->

## Setting Up OpenTrack                            <!-- GENERATED: opentrack -->

Carries the Webcam, Phone, Headset and Centring subsections. A repo may add a
subsection of its own (sleeping-dogs documents a FreeTrack shared-memory source
alongside the UDP one); --write then leaves the whole section alone and says so.

## Controls                                        <!-- hand-written -->

Read the bindings out of the repo's own source. They live in C#, C++, Rust, Lua
and Java across the fleet, and no data file records them correctly.

## Configuration                                   <!-- hand-written, with the GENERATED config block -->

Prose about the game's own settings stays hand-written. Once the repo's
committed config carries the [CameraUnlock] stamp, the section also holds the
config block, and a README without one fails conformance's `readme` check:

<!-- cameraunlock:config -->
(rendered, never edited by hand)
<!-- /cameraunlock:config -->

Each marker is a line of its own, with nothing else on it. The block says where
the file is installed; for a repo that published a pre-canonical build, the
one-time conversion, the `<file>.pre-canonical` and `<file>.pre-canonical.last`
copies, what is not carried over, and what an older version of the mod reads;
for a BepInEx mod, that ConfigurationManager does not list the settings, and
where it published a pre-canonical build, the `.ini` beside the untouched
`.cfg` in place of the copies, and how to reset; then the committed file itself
in an ini code fence. It replaces any hand-written key
listing, since the file's own comments describe every key.

`pixi run readme --write --sections config` inserts the block at the end of
Configuration (or adds the section when there is none) and keeps it current;
conformance fails a converted repo whose block differs, and an unconverted repo
that has one. NEXUS_MODS.md is untracked, so nothing checks it: paste the output
of `node cameraunlock-core/scripts/generate-readme.mjs --print config` into its
configuration section by hand, in the same change and again at every release.

## Notes                                           <!-- hand-written, optional -->

## Known Issues                                    <!-- hand-written, optional -->

## Troubleshooting                                 <!-- hand-written -->

## Updating                                        <!-- hand-written -->

## Uninstalling                                    <!-- hand-written -->

## Building from Source                            <!-- hand-written -->

## Community & Support                             <!-- GENERATED: community -->

## License                                         <!-- hand-written -->

Several repos extend the MIT line with a scope note, that the bundled loader
keeps its own licence or that the demo clip is the publisher's footage. Keep it.

## Credits                                         <!-- hand-written -->

## Disclaimer                                      <!-- hand-written -->

Names the publisher, so nothing can generate it.
