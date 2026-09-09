// modhub_hook.h - 模组内置 GMA 加载器（引擎内启动入口）
#pragma once

// 在客户端 DLL 初始化阶段（CHLClient::Init 末尾）调用一次。
// 扫描 <游戏目录>/mod/ 与 <游戏目录>/custom/mod_hub/mod/ 下的
// *.gma，自动解析 + CRC 校验 + 解包到 <游戏目录>/mod_unpacked/，
// 并把每个解包目录 AddSearchPath 进 GAME 路径头部。
void ModHub_RunStartup( const char *gameDir );
