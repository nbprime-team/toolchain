# toolchain 版本基线

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（LCD 已人工审查）

本文件记录 `toolchain/` 中本地 ARM 交叉工具链的**可复现基线**：包来源、校验和、
实测版本、目标架构参数，以及已知的打包布局差异。任何"构建通过"的结论都必须
能对应到本文件的某一组版本。

数值均由 `toolchain/scripts/verify-toolchain.sh` 与 `make versions` 在本仓库实测得出。

## 1. 来源包（`armtc/*.deb`）

全部为 Debian/Ubuntu 系统包，解包到 `armtc/root/` 即构成完整工具链。**这些包
体积合计约 600 MB，解包后约 3.4 GB，不纳入版本控制**（见 `.gitignore`）。

| 包 | 版本 | 架构 | SHA256 |
|---|---|---|---|
| `binutils-arm-none-eabi` | `2.45.50.20251209-1ubuntu1+23build1` | amd64 | `abccb894e824f007bd2f969e2164d164d308badf507910f105c3c3641345b416` |
| `gcc-arm-none-eabi` | `15:14.2.rel1-1` | amd64 | `43e626970e5b2c7b25bf03fb84508a4aff6d85216544038ff80bf6363787c12d` |
| `libnewlib-arm-none-eabi` | `4.6.0.20260123-1` | all | `8a58c66a59d5e57515cc3ca093f67599c682cb880992d87acb87145c47e33d8b` |
| `libnewlib-dev` | `4.6.0.20260123-1` | all | `5c90e34938803a087deba5f79417076a71193e4d60c451a26211abbf24190ab9` |
| `libstdc++-arm-none-eabi-dev` | `15:14.2.rel1-1+29` | all | `c28934e5400744a294c1650a35127f9aec248cccf904164d2ec2643e5487c4a1` |
| `libstdc++-arm-none-eabi-newlib` | `15:14.2.rel1-1+29` | all | `07cd65917323151f7e72ba6e7a9e019ffd7103084d1962108807832943a571a6` |

复算校验和：

```bash
cd toolchain/armtc && sha256sum *.deb
```

## 2. 工具实测版本

| 工具 | 版本输出 |
|---|---|
| `arm-none-eabi-gcc` | `arm-none-eabi-gcc (15:14.2.rel1-1) 14.2.1 20241119` |
| `arm-none-eabi-g++` | `arm-none-eabi-g++ (15:14.2.rel1-1) 14.2.1 20241119` |
| `arm-none-eabi-as` | `GNU assembler (2.45.50.20251209-1ubuntu1+23build1) 2.45.50.20251209` |
| `arm-none-eabi-ld` | `GNU ld (2.45.50.20251209-1ubuntu1+23build1) 2.45.50.20251209` |
| `arm-none-eabi-ar/objdump/readelf/nm/strip/objcopy/size` | binutils `2.45.50.20251209` |
| newlib（`libc.a` / `libm.a`） | `4.6.0.20260123-1` |
| libstdc++（newlib 变体） | `15:14.2.rel1-1+29` |

GCC 线程模型：`single`；LTO 压缩：`zlib`。

## 3. 目标架构（HP Prime G1）

| 参数 | 值 | 说明 |
|---|---|---|
| `-mcpu=` | `arm926ej-s` | Prime G1 为 ARM9（ARM926EJ-S）内核 |
| `-march=` | `armv5tej`（由 `-mcpu` 推导） | Version5 EABI |
| `-marm` | — | ARM 指令集（非 Thumb） |
| `-mfloat-abi=` | `soft` | 软浮点；`-mfpu=auto` 不适用 |
| multilib 库目录 | `usr/lib/arm-none-eabi/newlib/arm/v5te/softfp` | 软/软硬浮点共用 |

验证后的 ELF 属性（`readelf -h`）：

- 目标文件：`Type: REL`，`Machine: ARM`，`Flags: 0x5000000, Version5 EABI`
- 可执行文件：`Type: EXEC`，`Flags: 0x5000200, Version5 EABI, soft-float ABI`

## 4. 已知打包布局差异（务必阅读）

本工具链为 **Debian 打包版**，解包到 `armtc/root/` 后与"从 ARM 官网 tarball
安装"的布局不同，直接调用 `armtc/root/usr/bin/arm-none-eabi-gcc` 会失败：

1. **GCC 找不到自带的汇编器**。GCC driver 按**裸名** `as` 在 `PATH` 中查找子程序；
   工具链不在 `/usr` 前缀下，`PATH` 未准备时会命中宿主机的 x86 `as`，报
   `invalid -march= option: armv5tej` 之类的致命错误。
   → 由 `armtc-tools/` 包装层提供裸名入口解决。
2. **`collect2` 找不到链接器**。与驱动器不同，`collect2` 按 **target 前缀**
   `arm-none-eabi-ld` 查找，只提供裸名 `ld` 时链接阶段报
   `collect2: fatal error: cannot find 'ld'`。
   → 包装层同时提供裸名与 `arm-none-eabi-` 前缀两种入口（各 19 个），因此自足。
3. **newlib 头文件路径非标准**。头文件位于 `armtc/root/usr/include/newlib/`，
   而非 GCC 默认 sysroot 的 `usr/include/`，需要 `-isystem` 指出。
4. **specs 与 multilib 库目录需显式指定**。`nosys.specs`/`nano.specs` 位于
   `usr/lib/arm-none-eabi/newlib/`，库位于其 `arm/v5te/softfp/` 子目录，
   GCC 的 relocatable 前缀推导不会自动找到。

以上各点全部由 `armtc-tools/arm-none-eabi-gcc` 包装脚本注入，使用者只需
`source scripts/env.sh` 后正常调用 `arm-none-eabi-gcc` 即可。

## 5. 更新流程

升级工具链时：

1. 用新 `.deb` 替换 `armtc/*.deb`，运行 `make setup --force-unpack`（或
   `scripts/setup-toolchain.sh --force-unpack`）；
2. 运行 `make verify`，确认 21 项全通过；
3. 更新本文件的包版本、SHA256 与工具版本表；
4. 若 GCC 主版本变化，同步更新 `scripts/setup-toolchain.sh` 中的
   `GCC_INTERNAL` 路径（当前为 `14.2.1`）；
5. 通知 `prime-tcc/`、`app-collection/` 等下游工程复验构建。

## 6. 主机（PC）工具链基线

"PC 平台"在本仓库的定位是：**用宿主 gcc 编译同一份用户代码**（不链接运行，
见 [../prime-tcc/API_ABI_CONTRACT.md](../prime-tcc/API_ABI_CONTRACT.md)）。
基线由 `make host-check`（`scripts/check-host.sh`）生成，实测值：

| 工具 | 版本 / 取值 |
|---|---|
| `cc`（宿主 C 编译器） | `cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`；target `x86_64-linux-gnu` |
| `make` | `GNU Make 4.4.1` |
| `python3` | `Python 3.14.4` |
| 宿主 binutils（as/ld/ar/objdump/nm/readelf/objcopy） | `GNU Binutils for Ubuntu 2.46` |
| 数据模型 | 指针 64 位（**LP64**；ARM 目标为 ILP32） |

注意事项：

- 宿主的 `as`/`ld`（x86）与交叉工具链的 `arm-none-eabi-as`/`-ld` **同名不同物**：
  交叉编译必须经 `armtc-tools/` 包装层，包装脚本会把自身注入 `PATH` 最前；
- `size_t`/指针宽度在两侧不同，用户代码不得假设（见统一契约 §3、§4.2）。

## 7. 最近一次验证记录

| 项目 | 结果 |
|---|---|
| 验证脚本 | `make verify`（`scripts/verify-toolchain.sh`） |
| 结果 | **21 项通过，0 项失败** |
| 平台 | linux/amd64，Debian 系（dpkg 1.23.7） |
| 覆盖内容 | 工具可用性、裸名解析、包装层自足性、freestanding 编译、汇编归档、newlib 链接、头文件解析 |
| 下游复验 | `app-collection/staging/prime-code`（`primecode.elf`，24644 字节）与 `app-collection/examples/cube3d` 均可用本工具链重建 |
