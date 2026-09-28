# typeinfo

`extract_typeinfo.py` reads the id Tech 7 static type-info tables straight from a
`DOOMEternalx64vk.exe` file, without running the game or Meathook. It is the offline
prototype of the PLAN 2.4 type-info reader: the record layouts it documents are the
ones the C++ reader parses.

Requirements: Python 3.10+, `pefile`, `numpy`.

```
python extract_typeinfo.py path\to\DOOMEternalx64vk.exe --class idPlayer --grep "view|fov"
python extract_typeinfo.py path\to\DOOMEternalx64vk.exe --json classes.json
```

- Tables are located by scanning `.rdata` for runs of 0x48-byte class records, so the
  tool needs no per-build address. On Steam build 25216728 it finds two tables (engine,
  2873 records; game, 9290 records) and 12163 classes.
- `--class` walks the superclass chain unless `--no-inherited` is given.
- Output holds names, offsets, sizes and the engine's own field comments. Keep JSON
  dumps outside the repository; commit only the offsets a feature needs.

Record layouts and the build-specific results are in `docs/rig-findings/engine-facts.md`.
