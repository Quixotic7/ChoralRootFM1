# JieLi AC79 SDK files (Apache-2.0)

Three unmodified files from the JieLi AC79 SDK, tag `AC79NN_SDK_V1.2.1_2023-12-13`
(<https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>, `cpu/wl82/tools/`), kept here so the package builds
without a network fetch (the GitHub Actions release build cannot rely on gitee.com). Their copyright stays
with JieLi Technology; the licence is `LICENSE` in this folder (Apache License 2.0, the SDK's own; the SDK has
no NOTICE file). `tools/build.py` checks their SHA-256 (`SDK_SHA256`) before every build.

| file | what |
| --- | --- |
| `cpu/wl82/tools/uboot.boot` | the first-stage boot loader (SPL) |
| `cpu/wl82/tools/cfg_tool.bin` | the chip configuration block |
| `cpu/wl82/tools/cfg/eq_cfg_hw.bin` | the default hardware EQ table |

`build.sh` uses an SDK checkout at `$AC79_SDK` (default `~/fw-AC79_AIoT_SDK`) when one exists, else this folder.
