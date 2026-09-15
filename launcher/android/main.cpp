/*
Copyright (C) 2022 nillerusr

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of 
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <SDL_hints.h>
#include "tier0/dbg.h"
#include "tier0/threadtools.h"

char *LauncherArgv[512];
char java_args[4096];
int iLastArgs = 0;

// ModHub fork: 早期启动埋点。engine.log 要等引擎初始化后才写，
// 若崩在 Java/JNI/launcher 阶段则无任何文件证据。
// 这里在 launcher 的每个关键步骤写 launcher.log。
// 写多个候选路径（用户可见的 srceng / APP_DATA_PATH 私有目录 / 当前目录），
// 任何一个能写成功就写，避免"写进了用户看不到的目录"或"目录不存在写失败"。
static void ModHub_TraceLogPath( const char *path, const char *msg )
{
	FILE *f = fopen( path, "a" );
	if ( !f )
		return;
	fprintf( f, "%s\n", msg );
	fclose( f );
}

void ModHub_TraceLog( const char *fmt, ... )
{
	char msg[2048];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( msg, sizeof msg, fmt, ap );
	va_end( ap );

	static const char *cands[4];
	int nc = 0;
	const char *appdata = getenv( "APP_DATA_PATH" );
	if ( appdata && *appdata )
		cands[nc++] = appdata;
	cands[nc++] = "/storage/emulated/0/srceng";
	cands[nc++] = ".";

	char path[1024];
	for ( int i = 0; i < nc; i++ )
	{
		snprintf( path, sizeof path, "%s/launcher.log", cands[i] );
		ModHub_TraceLogPath( path, msg );
	}
}

// JNI_OnLoad：Java loadLibrary("launcher") dlopen 成功即调用。
// 这是比 LauncherMainAndroid 更早的探针——能确认 dlopen 是否真的发生。
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad( JavaVM *vm, void *reserved )
{
	ModHub_TraceLog( "[ModHub] JNI_OnLoad: launcher.so dlopen OK (step 0)" );
	return JNI_VERSION_1_6;
}

extern void InitCrashHandler();
DLL_EXPORT int LauncherMain( int argc, char **argv ); // from launcher.cpp

DLL_EXPORT int Java_com_valvesoftware_ValveActivity2_setenv(JNIEnv *jenv, jclass *jclass, jstring env, jstring value, jint over)
{
	Msg( "Java_com_valvesoftware_ValveActivity2_setenv %s=%s\n", jenv->GetStringUTFChars(env, NULL), jenv->GetStringUTFChars(value, NULL) );
	return setenv( jenv->GetStringUTFChars(env, NULL), jenv->GetStringUTFChars(value, NULL), over );
}

DLL_EXPORT void Java_com_valvesoftware_ValveActivity2_nativeOnActivityResult()
{
//	Msg( "Java_com_valvesoftware_ValveActivity_nativeOnActivityResult\n" );
}

DLL_EXPORT void Java_com_valvesoftware_ValveActivity2_setArgs(JNIEnv *env, jclass *clazz, jstring str)
{
	strncpy( java_args, env->GetStringUTFChars(str, NULL), sizeof java_args );
}

void SetLauncherArgs()
{
#define A(a,b) LauncherArgv[iLastArgs++] = (char*)a; \
	LauncherArgv[iLastArgs++] = (char*)b
#define D(a) LauncherArgv[iLastArgs++] = (char*)a

	static char binPath[2048];
	snprintf(binPath, sizeof binPath, "%s/hl2_linux", getenv("APP_DATA_PATH") );
	D(binPath);

	D("-nouserclip");

	char *pch;

	pch = strtok (java_args," ");
	while (pch != NULL)
	{
		LauncherArgv[iLastArgs++] = pch;
		pch = strtok (NULL, " ");
	}

	D("-fullscreen");
	D("-nosteam");
	D("-insecure");

#undef A
#undef D
}

float GetTotalMemory()
{
	int64_t mem = 0;

	char meminfo[8196] = { 0 };
	FILE *f = fopen("/proc/meminfo", "r");
	if( !f )
		return 0.f;

	size_t size = fread(meminfo, 1, sizeof(meminfo), f);
	if( !size )
		return 0.f;

	char *s = strstr(meminfo, "MemTotal:");

	if( !s ) return 0.f;

	sscanf(s+9, "%lld", &mem);
	fclose(f);

	return mem/1024/1024.f;
}

void android_property_print(const char *name)
{
	char prop[1024];

	char strValue[64];
	memset (strValue, 0, 64);
	snprintf(prop, sizeof(prop), "getprop %s", name);
	FILE *fp = NULL;
	fp = popen(prop, "r");
	if (!fp) return;

	fgets(strValue, sizeof(strValue), fp);
	pclose(fp);
	fp = NULL;

	Msg("prop %s=%s", name, strValue);
}


DLL_EXPORT int LauncherMainAndroid( int argc, char **argv )
{
	ModHub_TraceLog( "[ModHub] step 1: LauncherMainAndroid entered (launcher.so loaded OK), argc=%d", argc );
	InitCrashHandler();

	ModHub_TraceLog( "[ModHub] step 2: crash handler installed" );
	Msg("GetTotalMemory() = %.2f \n", GetTotalMemory());

	android_property_print("ro.build.version.sdk");
	android_property_print("ro.product.device");
	android_property_print("ro.product.manufacturer");
	android_property_print("ro.product.model");
	android_property_print("ro.product.name");

	ModHub_TraceLog( "[ModHub] step 3: props dumped" );
	SetLauncherArgs();

	ModHub_TraceLog( "[ModHub] step 4: launcher args set, iLastArgs=%d", iLastArgs );
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	DeclareCurrentThreadIsMainThread(); // Init thread propertly on Android

	ModHub_TraceLog( "[ModHub] step 5: SDL hint set, calling LauncherMain" );
	int ret = LauncherMain(iLastArgs, LauncherArgv);
	ModHub_TraceLog( "[ModHub] step 6: LauncherMain returned %d", ret );
	return ret;
}
