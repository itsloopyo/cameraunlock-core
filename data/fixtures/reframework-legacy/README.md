# REFramework legacy config fixtures

The config each REFramework head-tracking mod shipped at its newest published build, copied byte
for byte. cpp/tests/plugin_config_canonical_tests.cpp runs PluginConfig::Load and the RE legacy
import (PluginConfigLegacyImport) on each file and on every corpus input
(cameraunlock/config/testing/ini_mutations.h) built from it, and migrates each file through the
config owner to the canonical format.

| File | Repo | Build | Commit | Source | SHA-256 |
|------|------|-------|--------|--------|---------|
| resident-evil-2/HeadTracking.ini | itsloopyo/resident-evil-2-headtracking | `dev` pre-release | 5cc9ac6d5347fe729343fab9fdb30af8671a00e5 | `git show dev:HeadTracking.ini` | d54e354fab2399ac0391520ef7fbf49f577fc0a94157f6c23eef87dcfd862673 |
| resident-evil-3/HeadTracking.ini | itsloopyo/resident-evil-3-headtracking | `dev` pre-release | 234785c57051c49f345a67c5931a463d50500e0e | `git show dev:HeadTracking.ini` | 36687f292731c4bd93e6c0d6ba12d53b332778722bed27b43496b7c2aa650ee7 |
| resident-evil-4/HeadTracking.ini | itsloopyo/resident-evil-4-headtracking | `dev` pre-release | 3398b80c0f96e57ee59eba02c2c5173f692839e1 | `git show dev:HeadTracking.ini` | 5fce86bbc6c17556408499f667b632fa282dc71673b37cef880c622fb917fcd6 |
| resident-evil-7/HeadTracking.ini | itsloopyo/resident-evil-7-headtracking | `dev` pre-release | 8322e7a9cde74a9675b3ac1d7ec8529419f44b93 | `git show dev:HeadTracking.ini` | 396247b0cc7517cfaddcb1c98d6710fc7a3364081aa83be497df32349a1fc1c3 |
| resident-evil-village/HeadTracking.ini | itsloopyo/resident-evil-village-headtracking | `dev` pre-release | 56c327d3e8945dc1ff2f7cfea7ad5c1a4d5f1748 | `git show dev:HeadTracking.ini` | 072b12169a0038282ca1459ca74cd75f8926ede228691e9aece3535e8dd23e08 |
| resident-evil-requiem/HeadTracking.ini | itsloopyo/resident-evil-requiem-headtracking | v0.4.0 | 462ed779bca219692f10a6bf13cbc73a46a87d06 | `git show v0.4.0:HeadTracking.ini` | ce07a317f9a6146c52c459915a7073ac68492d8d1d74b7ead1a9afe5a155b34d |
| resident-evil-requiem/seed.ini | itsloopyo/resident-evil-requiem-headtracking | v0.4.0 | 462ed779bca219692f10a6bf13cbc73a46a87d06 | the `loader.seed` entry of `git show v0.4.0:launcher-manifest.json`, base64-decoded | 9ae8edfb101f6fbe594d4453a4b810b02ac9427c8bc02d0b2a61061cbf70261a |

Each HeadTracking.ini equals `plugins/HeadTracking.ini` in the build's installer ZIP on the
GitHub release, and requiem's v0.4.0 Nexus ZIP carries the same file. The five `dev` manifests
seed the same bytes as their HeadTracking.ini. Requiem's v0.4.0 manifest seeds an older file
instead (position sensitivity 2.0, invert keys, a reticle toggle, no flashlight or yaw keys),
which the launcher writes when the file is absent, so it is a fixture of its own. All checked
2026-09-24.
