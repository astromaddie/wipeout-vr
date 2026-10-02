# Vendored: sfvr

Shared Steam Frame VR port library (a small C99 library: system-layer View state machine, turn,
roomscale step, haptics table, settings key registry, `[vr-perf]` formatter).

| Item | Value |
| --- | --- |
| Canonical source | `~/code/sfvr` (a local, **uncommitted** work in progress: no commit hash exists) |
| Copied on | 2026-10-02 |
| Copied | `include/`, `src/`, `tests/` and `CMakeLists.txt`. Not copied: `Makefile`, `build/`, `.gitignore` |
| Local edits | none. Do not edit here; change the canonical source and re-copy |
| Build | `add_subdirectory(third_party/sfvr)` from the root `CMakeLists.txt` when `WIPEOUT_VR=ON`; the tests run through CTest |

To refresh: copy the same paths again, regenerate the table below with
`find include src tests CMakeLists.txt -type f | sort | xargs shasum -a 256`, and update the date.

## sha256 of each file

```
741591cc4783b7703fcc72bc82152d38005ca9e46e8579e3135433a3046006bd  CMakeLists.txt
55dd53d5bc481c8ad08b5dceea511a3da978216914e8371ffe540df3bf4b6407  include/sfvr/sfvr.h
4bc9a1c90f82dfd5d8e294b9f0b4318079d5d1bea3ddc590a3e8f23613fc9be0  include/sfvr/sfvr_haptics.h
29565f5b187a731a937a7770720bccd20bb834c443a646d202e1e0622715d7c4  include/sfvr/sfvr_perf.h
e7b593bc0f8a63e2b5be0a7acf890842aa4f63157805c27253c205e629ec4c51  include/sfvr/sfvr_room.h
6320aec23f429a9c5986b1d3ac0c096d4210358be8cd64c844196c6d51c57b9c  include/sfvr/sfvr_settings.h
d3d51354448f00c006f725cfe61043539dd7bba9fd6b935ded78e3f9c4e65174  include/sfvr/sfvr_turn.h
e421de82916fd218c8e2eeca8192e05be8f2071e461ac0a0b3706b41d17f038e  include/sfvr/sfvr_view.h
4192e234d3c7b4c0cec481c8b39efd606afc03495e1a576719dbf9035d88460c  src/sfvr_haptics.c
8ab8a144ed716d36713ed865bc3a2127620f9a5160ca2d3987ce083471bbbed3  src/sfvr_perf.c
a24a76a29441e86f3c9e07db862dfdf23dea4bc22def12822249a32187d70ea7  src/sfvr_room.c
142f4bb6fa166568256279dff350a4595a832c664281a6bbe33c73f8e9daff12  src/sfvr_settings.c
3cae4bfe2dab3248363eae8da9ed6d13fbbdddfcc2d904bc4547019afda7bfd6  src/sfvr_turn.c
2bc69993c9e5bf65c31c2b3497dde7fdbfa3801d382068b8d8a3b825584b94fe  src/sfvr_view.c
82122f093140ab897aeced0935165348e38e899012419542bdecc49f51e19d45  tests/sfvr_test.h
cd60876994649124fc8c607a7ffed5a9d256df81e98c8c4fc488183eaa95031c  tests/test_cxx.cpp
535e583114f3027037bd5acecf6144e251001a896ca6afdb06f8a6557750a25a  tests/test_haptics.c
1ead064c5b56775e38dd15314702b7704efc54f7182ffef50cfe4b3825cc47c3  tests/test_perf.c
cdd6fca67970fe8438a4f3603bf21537687f1f443eff616af3fc7d1cc71a4c24  tests/test_room.c
66d098bdbcae66381768df4486252daef2a4e47fb61f47a2cadf1f409f528e30  tests/test_settings.c
8a1c0bc0ad49d2a003449a7939f2f6c9ea57a861c558ad60bf16f4027f8d42c6  tests/test_turn.c
16e92cdac3a3f2f0c0e6a469edba6b42aa1c9ed9977976eca65e3a5b391209e0  tests/test_view.c
```
