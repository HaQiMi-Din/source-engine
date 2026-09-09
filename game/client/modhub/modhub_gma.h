// modhub_gma.h - 模组内置 GMA 解析/解包（引擎集成版，C++11 + POSIX）
//
// 与 hl2-mod-hub 仓库的 C++17 版 gma_parser 同源同逻辑，但仅依赖
// C++11 标准库与 POSIX（dirent/stat/open），可直接编进起源引擎的
// game/client 模块（引擎工程编译标准为 -std=c++11）。
//
// 支持两种真实 GMA 布局（自动检测）：
//  1) 现代布局 v2/v3（gmad / SharpGMad）：单字节版本头 + 空终止字符串
//     头 + 条目表 [序号u32][路径\0][大小u64][crcu32] + 紧贴数据区；
//  2) 经典布局 v1/v2（Valve Wiki 文档格式）：4 字节版本 + 条目表带偏移。
// 逐条目 CRC32（zlib 兼容表）校验决定选型，全部通过才认为格式可信。

#ifndef MODHUB_GMA_H
#define MODHUB_GMA_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace modhub {

struct ModEntry {
    std::string path;      // 包内相对路径（如 models/foo.mdl）
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
    std::uint64_t offset = 0;  // 数据在 .gma 文件中的偏移
};

struct ModGma {
    std::string name;      // 附加组件名（取自 .gma 头）
    std::string desc;      // 描述
    std::string author;    // 作者
    int         format = 0;    // 1=经典 v1/v2, 2=现代 v2/v3
    bool        crcOk = false; // 条目 CRC32 全部通过
    std::vector<ModEntry> entries;
};

// 解析 .gma 头与条目表（不做内容提取）。
// 返回 true 表示结构可读；crcOk 表示 CRC 校验是否全部通过。
bool ModParseGma( const char* path, ModGma& out, std::string& err );

// 把全部条目内容按包内相对路径提取到 outDir（自动创建父目录）。
// 返回提取条目数；失败时 err 描述原因。
std::size_t ModExtractAll( const char* gmaPath, const ModGma& gma,
                           const char* outDir, std::string& err );

// 扫描 dir 下所有 *.gma 文件名（不含子目录），返回完整路径列表。
std::vector<std::string> ModScanGma( const char* dir );

// 文件大小（-1 表示不存在/不可读）。
long long ModFileSize( const char* path );

// 递归创建目录（类似 mkdir -p）。
bool ModMkdirs( const char* path );

}  // namespace modhub

#endif  // MODHUB_GMA_H
