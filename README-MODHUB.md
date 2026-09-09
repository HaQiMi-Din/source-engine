# source-engine (HaQiMi-Din fork) — 内置 ModHub GMA 加载器

本 fork 基于 [nillerusr/source-engine](https://github.com/nillerusr/source-engine)
（安卓/PC 起源引擎移植），合入了 [HaQiMi-Din/hl2-mod-hub](https://github.com/HaQiMi-Din/hl2-mod-hub)
的 **模组内置 GMA 自动加载器**，使引擎**原生**具备以下能力（无需 AML、无需 Termux、无需外部工具）：

- 启动时自动扫描并解析 **GMA v1~v3**（GMod 附加组件容器）文件；
- 逐条目 CRC32 校验（zlib 兼容表）；
- 自动解包到 `<游戏目录>/mod_unpacked/<gma名>/`，并把每个解包目录
  `AddSearchPath` 进 `GAME` 路径头部；
- 幂等：`.modhub_done` 标记 + 文件大小比对，二次启动跳过解包直接挂载；
- 同时扫描两个位置：
  - `<游戏目录>/mod/*.gma`
  - `<游戏目录>/custom/mod_hub/mod/*.gma`（兼容 hl2-mod-hub 内容层部署）

## 集成位置

| 文件 | 说明 |
|---|---|
| `game/client/modhub/modhub_gma.{h,cpp}` | GMA 解析/解包核心（C++11 + POSIX，无引擎依赖，独立可测） |
| `game/client/modhub/modhub_hook.{h,cpp}` | 引擎钩子（SDK：扫描 + 解包 + AddSearchPath） |
| `game/client/cdll_client_int.cpp` | `CHLClient::Init` 末尾调用 `ModHub_RunStartup(engine->GetGameDirectory())` |
| `game/client/client_base.vpc` | ModHub 源文件加入 client 模块构建 |
| `scripts/build-android-arm64.sh` | 安卓 arm64（aarch64）构建脚本（NDK r10e + waf） |
| `.github/workflows/build.yml` | 新增 `build-android-arm64` job，产物上传为 Actions artifact |

## 安卓 arm64 构建（GitHub Actions 云编译）

推送本仓库后，`build-android-arm64` job 会用 NDK r10e 云编译安卓 arm64 引擎，
产物（`libclient.so` 等 + launcher）作为 artifact `source-engine-android-arm64`
输出（约 1~3 小时）。

拿到产物后的用法：
1. 用 apktool 解包你现有的起源引擎 APK；
2. 用本构建的 `lib/*.so` 替换 `lib/arm64-v8a/` 下同名文件；
3. 重打包 + 签名 + 安装（先卸载原版，注意备份存档）；
4. 把 `.gma` 丢进 `/sdcard/srceng/hl2/mod/`（或 `hl2/custom/mod_hub/mod/`），
   启动后日志出现 `[ModHub] 已挂载 ...` 即成功。

> 版权提示：本仓库源于泄漏的 Source 源码，fork 仅用于自用研究，
> 请勿公开分发构建产物。

## 独立测试（不含 SDK）

```sh
g++ -std=c++11 -O2 game/client/modhub/modhub_gma_test.cpp \
    game/client/modhub/modhub_gma.cpp -o mgtest
./mgtest your_addon.gma
```
