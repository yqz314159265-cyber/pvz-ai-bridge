# Vendored source references

These files were fetched from the official upstream repositories at the stated immutable releases. The Git blob IDs below are SHA-1 object IDs returned by GitHub's contents API, useful for checking each fetched source file against upstream.

## MinHook 1.3.4

Repository: https://github.com/TsudaKageyu/MinHook  
Release: https://github.com/TsudaKageyu/MinHook/tree/v1.3.4  
Tag commit: `c3fcafdc10146beb5919319d0683e44e3c30d537`

| Vendored file | Upstream file | Git blob SHA-1 |
|---|---|---|
| `native/vendor/minhook/LICENSE.txt` | `LICENSE.txt` | `74dea27229c05b53b095aa22b9ee7ee9f549e414` |
| `native/vendor/minhook/include/MinHook.h` | `include/MinHook.h` | `492d83f8d6245f576a703cd6a0bd358cd3aa2d9c` |
| `native/vendor/minhook/src/buffer.c` | `src/buffer.c` | `55412b0f301ba9a5b3520343bd1313bb56592899` |
| `native/vendor/minhook/src/buffer.h` | `src/buffer.h` | `204d551bffb7f11760bc1e474aa835e55f4cbb00` |
| `native/vendor/minhook/src/hook.c` | `src/hook.c` | `68a006a768c884372a34ee34043e20db079deba6` |
| `native/vendor/minhook/src/trampoline.c` | `src/trampoline.c` | `d4d6f99d9fb23d980b12eb0ba4f722e5f8377d87` |
| `native/vendor/minhook/src/trampoline.h` | `src/trampoline.h` | `bdffdac0a7bc838b52dd35733c943a9d5c7bc7b5` |
| `native/vendor/minhook/src/hde/hde32.c` | `src/hde/hde32.c` | `eb6af9b8ac8f3d83ab370f807467f2733fb4210d` |
| `native/vendor/minhook/src/hde/hde32.h` | `src/hde/hde32.h` | `1112450d87ae6772ad805f4f7fa07965c5bbbdb0` |
| `native/vendor/minhook/src/hde/table32.h` | `src/hde/table32.h` | `7b3e12e32fdb7c362ca333c992e3c2e05556edb5` |
| `native/vendor/minhook/src/hde/pstdint.h` | `src/hde/pstdint.h` | `84d82a012311a72f5dfb1fc88554f7820fbdf216` |

The x86 library target needs these C sources: `buffer.c`, `hook.c`, `trampoline.c`, and `hde/hde32.c`. Add include paths `native/vendor/minhook/include` and `native/vendor/minhook/src`; the HDE source includes its adjacent headers.

## nlohmann/json 3.12.0

Repository: https://github.com/nlohmann/json  
Release: https://github.com/nlohmann/json/tree/v3.12.0
Tag commit: `55f93686c01528224f448c19128836e7df245f72`

| Vendored file | Upstream file | Git blob SHA-1 |
|---|---|---|
| `native/vendor/nlohmann/LICENSE.MIT` | `LICENSE.MIT` | `a1dacc8dbbd907c4b622ff1f08e279c27465dcbc` |
| `native/vendor/nlohmann/json.hpp` | `single_include/nlohmann/json.hpp` | `82d69f7c5d044c9887c96b90c97f5639083ecd14` |

The single header is `native/vendor/nlohmann/json.hpp`, fetched from `single_include/nlohmann/json.hpp` at tag `v3.12.0`. Include `native/vendor/nlohmann` and use `#include <json.hpp>`; alternatively include the parent `native/vendor` and use `#include <nlohmann/json.hpp>`.
