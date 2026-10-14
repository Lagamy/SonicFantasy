# Station Square (SADX) import notes

Working notes and reference for the Station Square import. Kept compact on purpose: no separate docs/ai twin.
Evidence tags: decompile = read from the disc files by a script; hypothesis = our guess.

## Land tables (decompile, 2026-10-13, `_Adv00.rel`, pointers relocated as Rel.cpp does)

| Table | Offset | Pieces | Extents (x, y, z) | Texture archive | Texture names suggest (hypothesis) |
|---|---|---|---|---|---|
| landtabless00 | 0x1FFA38 | 266 | -255..852, -414..640, -89..1246 | advSS00.GVM | city streets: city office, main road, park flowers, shops |
| landtabless01 | 0x2035DC | 423 | -867..569, -542..904, 327..1759 | advSS01.GVM | hotel / casino (bst_*, cst_cajino*, ss_hotel*) |
| landtabless02 | 0x20474C | 123 | 288..620, -225..87, 565..1362 | advSS02.GVM | sewer / pipes (ss_ges*) |
| landtabless03 | 0x206F3C | 283 | -3728..4168, -3998..3930, 472..8368 | advSS03.gvm | station plaza: station name, kiosks, magazines, billboards, Twinkle Park gate |
| landtabless04 | 0x207E00 | 104 | -4190..2915, -3593..3512, -674..6431 | advSS04.GVM | beach side: sea, sand, pier (ss_sanbashi), palms, pool, hotel door |
| landtabless05 | 0x20811C | 21 | 561..948, -31..177, 1590..1854 | advSS05.GVM | Twinkle Park entrance (tw*) |

Extents = min/max of COL centre -/+ radius over all pieces.

- Texture archive of table N is advSS0N.GVM (hypothesis from the names; confirmed for 04: the 92 names in advSS04.GVM equal the table's texlist, same order; all textures are CMPR or RGB5A3).
- Placement files on disc per act: SetSS0NS.bin (S = Sonic; a b e k l m = other characters), camera files camss0Ns.bin.
- Chosen for the import (observed, user, 2026-10-13): landtabless04, "the first section near the Emerald Coast". The table index 4 is not "first" by index; the user picked it from the texture evidence above.

## Level files (2026-10-14, not run)

- One level per land table, named `StationSquare<table index>`: `Levels/StationSquare0` .. `StationSquare5` = landtabless00..05 of `_Adv00.rel` (offsets in the table above), textures `advSS0N.GVM` (03: `advSS03.gvm`). Each file: engine sadx, shader SA, landTable and textures only; geometry, textures and collision, no entities. They are placed in the map by the user (Map tab: Add level to map); only `StationSquare4` is in Map.json.
- `StationSquare4` is the former `StationSquare` (level files renamed 2026-10-14; Map.json entry renamed, place unchanged). Its `exclude` list has a box that removes the giant flat floor (decompile of landtabless04, 2026-10-14: piece 73, 49 vertices all at y -40.5, x -3118..1842, z 337..5420, texture list index 0 `ast_hasi19` by the material read, which may be wrong: the user called it a sand plane). Box x -3130..1850, y -41..-40.2, z 330..5430: holds the piece's sphere centre (-638, -40.5, 2878) and its collision triangles; pieces 69 / 70 / 53 have vertices at y -40 / -39 and stay outside it. The second box in the file is the user's own (pier area).
- `Scripts/game.lua`: `"StationSquare4"` is second in `ORDER`; `StationSquare0 1 2 3 5` are SADX levels with the placeholder start, reached by Load level in the editor.
- Start position of `StationSquare4` is a placeholder (hypothesis): above piece 24 (centre -332, 0, 1715, radius 140; decompile of the table), players 150 up, camera 135 behind in +z as in EmeraldCoast. The real start of the act is unknown.
- Disc paths: `_Adv00.rel` and the `advSS0N.GVM` are at the disc root, like `_Stg01.rel` and `BEACH01.GVM` (ResourceMap.txt). Only `advSS04.GVM` was known to exist before this change (not run for the others).
- Unknown until run: whether the tables 0, 1, 2, 3 and 5 import without a throw, and how they look.
