# Contributing

Keep this repository reviewable without a phone.

- Run `python3 tools/verify-x6886-final.py` before committing build changes.
  It must report `13/13`.
- Keep device-specific constants in `devices/x6886/` and shared chip facts in
  `socs/mt6789/`.
- Put kernel drivers under `drivers/<subsystem>/` and their evidence notes under
  `docs/driver-review/`.
- Cite the dump file and line number for every hardware claim. Mark anything
  inferred from another phone as reference-only.
- Do not add `ios27/`, `refs/`, `tools/root/`, `tools/bin/`, generated images,
  logs, or captured ramoops files to git.
- Never flash from a script in this tree. Device writes require an explicit,
  separate decision.

See [docs/index.md](docs/index.md) for the documentation map.
