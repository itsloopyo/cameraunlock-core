# OWML packages in Lopari

Outer Wilds uses `delivery_mode: "external"`. OWMM installs and maintains
OWML. A package opts into Lopari deployment with
`external.owml_mod_id: "itsloopyo.OuterWildsHeadTracking"` and `strategy: "OWML"`.
External packages without that field keep their existing manager-only route.

Lopari resolves OWMM's `owmlPath` from its settings and checks the loader's
configured game directory against the selected installation. For this package,
`mod_home` resolves to the mod's folder under that OWML installation's `Mods`
directory. An existing installation is identified by its OWML `uniqueName`;
otherwise the new folder uses that ID. Package metadata never supplies an
absolute deployment path.

The four `files` entries use `anchor: "mod_home"` and target
`OuterWildsHeadTracking.dll`, `CameraUnlock.Core.dll`, `manifest.json`, and
`default-config.json`. Loader provisioning, variants, patches, runtime requirements,
and dependencies are not accepted on this route. `config.json` belongs to the
player and is excluded from deployment, repair, and removal. Play enables a
disabled mod by changing only its `enabled` setting.

The launcher records deployed files in its ordinary receipt. Launch verifies
owned files and repairs them from the catalog package if needed. OWMM-installed
copies can launch without a Lopari receipt. Missing installs need a downloadable
catalog package or installation through OWMM. Local development can supply the
rebuilt installer ZIP through Lopari's `pixi run go-local`.

The package validator checks the opted-in payload and rejects missing sources.
Older Lopari builds do not support this opt-in; OWMM can still consume the ZIP.
