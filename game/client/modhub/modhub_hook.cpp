// modhub_hook.cpp - 模组内置 GMA 加载器（起源引擎集成实现）
//
// 依赖 Source SDK（game/client 模块内编译）：cbase.h / filesystem.h。
// 由 cdll_client_int.cpp 的 CHLClient::Init 调用 ModHub_RunStartup()。
//
// 挂载约定：
//   <游戏目录>/mod/*.gma                    放 GMod 附加组件
//   <游戏目录>/custom/mod_hub/mod/*.gma     兼容 hl2-mod-hub 内容层部署
//   <游戏目录>/mod_unpacked/<gma名>/        自动解包输出（AddSearchPath 挂载）
// 幂等：解包完成后在输出目录写 .modhub_done 标记（记录 .gma 大小），
// 二次启动时大小未变则跳过解包，直接挂载。

#include "cbase.h"
#include "filesystem.h"
#include "modhub_gma.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern IFileSystem *filesystem;

namespace {

// 标记文件内容为 .gma 文件大小；一致则视为已解包（幂等跳过）
bool IsUpToDate( const char *doneMarker, const char *gmaPath )
{
    FILE *f = fopen( doneMarker, "rb" );
    if ( !f )
        return false;
    char buf[64];
    size_t n = fread( buf, 1, sizeof( buf ) - 1, f );
    fclose( f );
    buf[n] = '\0';
    return atoll( buf ) == modhub::ModFileSize( gmaPath );
}

void WriteDoneMarker( const char *doneMarker, const char *gmaPath )
{
    FILE *f = fopen( doneMarker, "wb" );
    if ( !f )
        return;
    fprintf( f, "%lld", modhub::ModFileSize( gmaPath ) );
    fclose( f );
}

void MountGmaDir( const char *gameDir, const char *modSubDir )
{
    std::string modDir = std::string( gameDir ) + "/" + modSubDir;
    std::vector<std::string> gmas = modhub::ModScanGma( modDir.c_str() );
    if ( gmas.empty() )
        return;

    for ( size_t i = 0; i < gmas.size(); ++i )
    {
        const std::string &gmaPath = gmas[i];

        modhub::ModGma gma;
        std::string err;
        if ( !modhub::ModParseGma( gmaPath.c_str(), gma, err ) )
        {
            Warning( "[ModHub] 解析失败 %s: %s\n", gmaPath.c_str(), err.c_str() );
            continue;
        }
        if ( !gma.crcOk )
        {
            Warning( "[ModHub] 跳过 %s: CRC 校验未通过\n", gmaPath.c_str() );
            continue;
        }

        // 解包目录名取 .gma 文件名（去扩展名）
        size_t slash = gmaPath.find_last_of( '/' );
        std::string base =
            ( slash == std::string::npos ) ? gmaPath : gmaPath.substr( slash + 1 );
        if ( base.size() > 4 )
            base.resize( base.size() - 4 );  // 去掉 .gma

        std::string outDir = std::string( gameDir ) + "/mod_unpacked/" + base;
        std::string doneMarker = outDir + "/.modhub_done";

        if ( !IsUpToDate( doneMarker.c_str(), gmaPath.c_str() ) )
        {
            if ( !modhub::ModMkdirs( outDir.c_str() ) )
            {
                Warning( "[ModHub] 无法创建解包目录 %s\n", outDir.c_str() );
                continue;
            }
            std::string xerr;
            size_t n = modhub::ModExtractAll(
                gmaPath.c_str(), gma, outDir.c_str(), xerr );
            WriteDoneMarker( doneMarker.c_str(), gmaPath.c_str() );
            if ( !xerr.empty() )
                Warning( "[ModHub] %s 部分失败: %s\n", base.c_str(), xerr.c_str() );
            Msg( "[ModHub] 解包 %s -> mod_unpacked/%s (%zu 条目)\n",
                 base.c_str(), base.c_str(), n );
        }

        filesystem->AddSearchPath( outDir.c_str(), "GAME", PATH_ADD_TO_HEAD );
        Msg( "[ModHub] 已挂载 %s (%zu 条目, CRC OK)\n",
             base.c_str(), gma.entries.size() );
    }
}

}  // namespace

void ModHub_RunStartup( const char *gameDir )
{
    if ( !gameDir || !gameDir[0] || !filesystem )
    {
        Warning( "[ModHub] 跳过：游戏目录或文件系统接口不可用\n" );
        return;
    }

    Msg( "[ModHub] 启动：扫描 %s 下的 GMA 附加组件\n", gameDir );
    MountGmaDir( gameDir, "mod" );
    MountGmaDir( gameDir, "custom/mod_hub/mod" );
    Msg( "[ModHub] 自动挂载完成\n" );
}
