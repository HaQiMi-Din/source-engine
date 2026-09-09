// modhub_gma_test - 引擎集成版 GMA 解析/解包独立测试
#include "modhub_gma.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法: %s <file.gma>\n", argv[0]);
        return 2;
    }
    modhub::ModGma gma;
    std::string err;
    const bool ok = modhub::ModParseGma(argv[1], gma, err);
    std::printf("parse=%d format=%d name=%s crcOk=%d entries=%zu err=%s\n",
                ok, gma.format, gma.name.c_str(), gma.crcOk,
                gma.entries.size(), err.c_str());
    if (!ok || !gma.crcOk)
        return 1;
    for (size_t i = 0; i < gma.entries.size(); ++i) {
        std::printf("  [%zu] %s (%llu bytes, crc=%08x)\n", i,
                    gma.entries[i].path.c_str(),
                    (unsigned long long)gma.entries[i].size,
                    gma.entries[i].crc);
    }
    std::string xerr;
    const size_t n = modhub::ModExtractAll(
        argv[1], gma, "/tmp/modhub_extract_test", xerr);
    std::printf("extracted=%zu err=%s\n", n, xerr.c_str());
    return (n == gma.entries.size()) ? 0 : 1;
}
