# Contributing

## Before you send anything

```sh
python3 tools/verify-x6886-final.py
```

It must print `13/13`. It checks the final boot image, the committed loader
blob, the loader ELF under `/tmp/hakios-x6886/elf-final2/`, and the board and
loader sources. It never touches the phone.

If it prints `6/13`, the scratch ELF is missing:

```sh
bash tools/build-loader-elf-x6886.sh
python3 tools/verify-x6886-final.py
```

## Where a change goes

| The change | Goes in | Then |
| --- | --- | --- |
| A device value, board file, or dump capture | `devices/x6886/` | Name the dump file and line in the note. |
| A shared MT6789 value | `socs/mt6789/soc.toml` | `tools/boards.py check`, if the package is present in `refs/`. |
| A kernel driver | `drivers/<subsystem>/` | Its evidence note goes in `docs/driver-review/`. |
| Loader code | `xnuloader/` | Rebuild the ELF, then run the verifier. |
| A build or check script | `tools/` | Run the script once and paste its output into the commit body. |
| A boot result | `docs/plan/status.md` | Quote the log line. Do not write "works" without it. |

[docs/index.md](docs/index.md) maps the rest of the tree.

## Rules

- An address or pin that came from the dump is a fact. Name the source. An
  address borrowed from another phone stays marked as reference-only.
- A driver starts off by default. Reading the hardware first, writing only
  when the boot argument asks for it, is still the rule.
- Say what was checked: `13/13`, a compile, a QEMU boot, or a line seen on the
  phone are four different claims.
- Do not commit `ios27/`, `refs/`, `tools/root/`, `tools/bin/`, boot images,
  logs, or ramoops captures. They are local inputs.
- No script here flashes a partition. Device writes are a separate decision,
  made with the phone present.

## Commits

One change per commit. The body says what changed, how it was checked, and what
was not checked. Name the paths. A line quoted from a boot log is worth more
than an adjective.

## Style

C follows the files around it: tabs, comments that say why, no phone name in a
symbol. Shell scripts use bash. Python uses only the standard library.
