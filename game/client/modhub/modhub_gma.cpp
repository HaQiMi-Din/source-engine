// modhub_gma.cpp - 模组内置 GMA 解析/解包（引擎集成版，C++11 + POSIX）
//
// 与 hl2-mod-hub 仓库 src/ModHubCore/gma_parser.cpp 同源同逻辑，
// 仅用 C++11 标准库 + POSIX（dirent/stat/mkdir/open），不依赖
// std::filesystem，可编进引擎 game/client 模块。

#include "modhub_gma.h"

#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <fstream>
#include <string>

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace modhub {
namespace {

// C++98 兼容的整数转字符串（引擎编译标准 gnu++98，无 std::to_string）
std::string Itoa(unsigned long long v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%llu", v);
	return std::string(buf);
}

bool ReadBytes(std::ifstream& f, void* dst, std::size_t n) {
    f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(f.gcount()) == n;
}

std::uint32_t LE32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t LE64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | p[i];
    }
    return v;
}

std::string FixedString(const unsigned char* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != '\0') {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(p), len);
}

// 读取一个空终止字符串（含终止符），上限 64MB
bool ReadNullTerm(std::ifstream& f, std::string& out, std::string& err) {
    out.clear();
    char c;
    while (f.get(c)) {
        if (c == '\0') {
            return true;
        }
        out += c;
        if (out.size() > (1u << 26)) {
            err = "字符串过长（超过 64MB），文件可能损坏";
            return false;
        }
    }
    err = "字符串未找到终止符";
    return false;
}

void BuildCrcTable(std::uint32_t table[256]) {
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        }
        table[i] = c;
    }
}

// CRC32 (IEEE 802.3, 与 zlib crc32 一致)
std::uint32_t Crc32(const unsigned char* data, std::size_t len) {
    static std::uint32_t kTable[256];
    static bool kInit = false;
    if (!kInit) {
        BuildCrcTable(kTable);
        kInit = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c = kTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

// 逐条目校验 CRC32（crc==0 视为未知，跳过）
bool VerifyCrc(const std::string& path, ModGma& gma) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        return false;
    }
    std::vector<unsigned char> data;
    for (std::size_t i = 0; i < gma.entries.size(); ++i) {
        const ModEntry& e = gma.entries[i];
        if (e.crc == 0 || e.size == 0) {
            continue;
        }
        f.clear();
        f.seekg(static_cast<std::streamoff>(e.offset));
        if (!f) {
            return false;
        }
        data.resize(static_cast<std::size_t>(e.size));
        if (!ReadBytes(f, &data[0], data.size())) {
            return false;
        }
        if (Crc32(&data[0], data.size()) != e.crc) {
            return false;
        }
    }
    return true;
}

// ---- 现代布局（当前 gmad 写入格式，SharpGMad 读取器兼容）----
bool ParseModern(const std::string& path, ModGma& out, std::string& err) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        err = "无法打开文件: " + path;
        return false;
    }

    unsigned char sig[5];
    if (!ReadBytes(f, sig, sizeof(sig))) {
        err = "文件过短，头部不完整";
        return false;
    }
    if (!(sig[0]=='G' && sig[1]=='M' && sig[2]=='A' && sig[3]=='D')) {
        err = "缺少 GMAD 签名，不是 GMA 文件";
        return false;
    }
    const std::uint32_t version = sig[4];
    if (version == 0 || version > 3) {
        err = "不支持的 GMA 版本: " + Itoa(version);
        return false;
    }

    unsigned char b[16];
    if (!ReadBytes(f, b, sizeof(b))) {
        err = "头部字段读取失败";
        return false;
    }
    if (version >= 2) {
        // required content: 空终止字符串列表，空串结束
        std::string s;
        do {
            if (!ReadNullTerm(f, s, err)) {
                return false;
            }
        } while (!s.empty());
    }

    if (!ReadNullTerm(f, out.name, err)) return false;
    if (!ReadNullTerm(f, out.desc, err)) return false;
    if (!ReadNullTerm(f, out.author, err)) return false;

    unsigned char ver4[4];
    if (!ReadBytes(f, ver4, sizeof(ver4))) {
        err = "addon 版本读取失败";
        return false;
    }

    // 条目表: [序号 u32][路径\0][大小 u64][crc u32] ... 序号 0 结束
    out.entries.clear();
    std::uint64_t prevSeq = 0;
    std::uint64_t relOffset = 0;
    std::vector<std::uint64_t> rels;
    for (;;) {
        unsigned char seqb[4];
        if (!ReadBytes(f, seqb, sizeof(seqb))) {
            err = "条目表读取失败（条目序号处截断）";
            return false;
        }
        const std::uint64_t seq = LE32(seqb);
        if (seq == 0) {
            break;
        }
        if (seq <= prevSeq) {
            err = "条目序号不递增（不是现代布局或文件损坏）";
            return false;
        }
        prevSeq = seq;

        std::string name;
        if (!ReadNullTerm(f, name, err)) {
            return false;
        }
        unsigned char ent[12];
        if (!ReadBytes(f, ent, sizeof(ent))) {
            err = "条目信息读取失败";
            return false;
        }
        ModEntry e;
        e.path = name;
        e.size = LE64(ent);
        e.crc = LE32(ent + 8);
        e.offset = relOffset;  // 相对表尾，稍后修正
        rels.push_back(relOffset);
        relOffset += e.size;
        out.entries.push_back(e);
    }

    if (out.entries.empty()) {
        err = "条目表为空";
        return false;
    }

    const std::uint64_t tableEnd = static_cast<std::uint64_t>(f.tellg());
    for (std::size_t i = 0; i < out.entries.size(); ++i) {
        out.entries[i].offset = tableEnd + rels[i];
    }

    // 数据区边界检查（允许尾部 ≤64 字节冗余）
    const long long fileSize = ModFileSize(path.c_str());
    const std::uint64_t dataEnd = tableEnd + relOffset;
    if (fileSize >= 0 &&
        (dataEnd > static_cast<std::uint64_t>(fileSize) ||
         static_cast<std::uint64_t>(fileSize) - dataEnd > 64)) {
        err = "数据区与文件大小不吻合（文件损坏或非现代布局）";
        return false;
    }

    out.format = 2;  // 现代布局统一记 2
    return true;
}

// ---- 经典布局（Valve Wiki 文档格式）----
bool ParseClassic(const std::string& path, ModGma& out, std::string& err) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        err = "无法打开文件: " + path;
        return false;
    }

    const std::size_t kHeaderSize =
        4 + 4 + 8 + 8 + 8 + 256 + 65536 + 128 + 4;
    std::vector<unsigned char> hdr(kHeaderSize);
    if (!ReadBytes(f, &hdr[0], kHeaderSize)) {
        err = "文件过短，头部不完整（经典布局）";
        return false;
    }
    if (!(hdr[0]=='G' && hdr[1]=='M' && hdr[2]=='A' && hdr[3]=='D')) {
        err = "缺少 GMAD 签名，不是 GMA 文件";
        return false;
    }

    std::size_t o = 4;
    const std::uint32_t version = LE32(&hdr[o]); o += 4;
    if (version == 0 || version > 3) {
        err = "不支持的 GMA 版本: " + Itoa(version);
        return false;
    }
    out.format = 1;  // 经典布局统一记 1
    o += 8;  // steam_id
    o += 8;  // timestamp
    if (version >= 2) {
        o += 8;  // required_content
    }
    out.name = FixedString(&hdr[o], 256); o += 256;
    out.desc = FixedString(&hdr[o], 65536); o += 65536;
    out.author = FixedString(&hdr[o], 128); o += 128;
    o += 4;  // addon_version

    out.entries.clear();
    for (;;) {
        unsigned char lenBuf[4];
        if (!ReadBytes(f, lenBuf, 4)) {
            err = "条目表读取失败（可能在条目名长度处截断）";
            return false;
        }
        const std::uint32_t nameLen = LE32(lenBuf);
        if (nameLen == 0) {
            break;
        }
        if (nameLen > (1u << 20)) {
            err = "条目名长度异常: " + Itoa(nameLen);
            return false;
        }
        std::string name(nameLen, '\0');
        if (!ReadBytes(f, &name[0], nameLen)) {
            err = "条目名读取失败";
            return false;
        }
        unsigned char ent[8 + 4 + 8];
        if (!ReadBytes(f, ent, sizeof(ent))) {
            err = "条目信息读取失败";
            return false;
        }
        ModEntry e;
        e.path = name;
        e.size = LE64(ent);
        e.crc = LE32(ent + 8);
        e.offset = LE64(ent + 12);
        out.entries.push_back(e);
    }

    if (out.entries.empty()) {
        err = "条目表为空";
        return false;
    }

    // 偏移边界检查
    const long long fileSize = ModFileSize(path.c_str());
    if (fileSize >= 0) {
        for (std::size_t i = 0; i < out.entries.size(); ++i) {
            const ModEntry& e = out.entries[i];
            if (e.offset + e.size > static_cast<std::uint64_t>(fileSize)) {
                err = "条目偏移越界（文件损坏或非经典布局）";
                return false;
            }
        }
    }

    return true;
}

}  // namespace

long long ModFileSize(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return static_cast<long long>(st.st_size);
}

bool ModMkdirs(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    std::string cur;
    if (path[0] == '/') {
        cur = "/";
    }
    std::string rest = path;
    if (cur == "/") {
        rest = rest.substr(1);
    }
    std::size_t pos = 0;
    while (pos <= rest.size()) {
        std::size_t slash = rest.find('/', pos);
        if (slash == std::string::npos) {
            cur += rest.substr(pos);
        } else {
            cur += rest.substr(pos, slash - pos);
        }
        if (!cur.empty() && cur != "/") {
            if (mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST) {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        cur += '/';
        pos = slash + 1;
    }
    return true;
}

bool ModParseGma(const char* path, ModGma& out, std::string& err) {
    if (!path) {
        err = "空路径";
        return false;
    }
    const std::string p(path);

    ModGma modern;
    std::string modernErr;
    const bool modernOk = ParseModern(p, modern, modernErr);
    if (modernOk && VerifyCrc(p, modern)) {
        out = modern;
        out.crcOk = true;
        return true;
    }

    ModGma classic;
    std::string classicErr;
    const bool classicOk = ParseClassic(p, classic, classicErr);
    if (classicOk && VerifyCrc(p, classic)) {
        out = classic;
        out.crcOk = true;
        return true;
    }

    // 两种布局均未能通过 CRC 校验：优先返回结构可读的一方并标注
    if (modernOk) {
        out = modern;
        out.crcOk = false;
        err = "CRC 校验未通过（文件可能损坏）: " + modernErr;
        return true;
    }
    if (classicOk) {
        out = classic;
        out.crcOk = false;
        err = "CRC 校验未通过（文件可能损坏）: " + classicErr;
        return true;
    }
    err = "无法解析为任何已知 GMA 布局（现代: " + modernErr +
          "; 经典: " + classicErr + "）";
    return false;
}

std::size_t ModExtractAll(const char* gmaPath, const ModGma& gma,
                          const char* outDir, std::string& err) {
    if (!gmaPath || !outDir) {
        err = "空参数";
        return 0;
    }
    std::size_t ok = 0;
    for (std::size_t i = 0; i < gma.entries.size(); ++i) {
        const ModEntry& entry = gma.entries[i];

        // 安全：拒绝绝对路径与路径穿越
        const std::string& rel = entry.path;
        if (rel.empty() || rel[0] == '/' || rel[0] == '\\' ||
            rel.find("..") != std::string::npos) {
            if (err.empty()) {
                err = "非法条目路径: " + rel;
            }
            continue;
        }

        std::string dest = std::string(outDir) + "/" + rel;
        // 父目录
        std::size_t slash = dest.find_last_of('/');
        std::string parent =
            (slash == std::string::npos) ? std::string(outDir)
                                         : dest.substr(0, slash);
        if (!ModMkdirs(parent.c_str())) {
            if (err.empty()) {
                err = "无法创建目录: " + parent;
            }
            continue;
        }

        std::ifstream f(gmaPath, std::ios::binary);
        if (!f) {
            if (err.empty()) {
                err = "无法打开 .gma: " + std::string(gmaPath);
            }
            continue;
        }
        f.seekg(static_cast<std::streamoff>(entry.offset));
        if (!f) {
            if (err.empty()) {
                err = "无法定位条目数据: " + rel;
            }
            continue;
        }

        // 用 C 风格写出（安卓上 ofstream 亦可，统一用 FILE* 稳妥）
        FILE* out = fopen(dest.c_str(), "wb");
        if (!out) {
            if (err.empty()) {
                err = "无法写入文件: " + dest;
            }
            continue;
        }
        std::vector<char> buf(1 << 16);
        std::uint64_t remaining = entry.size;
        bool failed = false;
        while (remaining > 0) {
            const std::size_t chunk = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buf.size()));
            if (!ReadBytes(f, &buf[0], chunk)) {
                if (err.empty()) {
                    err = "读取条目数据失败: " + rel;
                }
                failed = true;
                break;
            }
            if (fwrite(&buf[0], 1, chunk, out) != chunk) {
                if (err.empty()) {
                    err = "写入条目数据失败: " + rel;
                }
                failed = true;
                break;
            }
            remaining -= chunk;
        }
        fclose(out);
        if (!failed) {
            ++ok;
        }
    }
    return ok;
}

std::vector<std::string> ModScanGma(const char* dir) {
    std::vector<std::string> out;
    if (!dir) {
        return out;
    }
    DIR* d = opendir(dir);
    if (!d) {
        return out;
    }
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        const char* name = de->d_name;
        const std::size_t len = strlen(name);
        if (len > 4 &&
            strcasecmp(name + len - 4, ".gma") == 0) {
            std::string full = std::string(dir) + "/" + name;
            out.push_back(full);
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace modhub
