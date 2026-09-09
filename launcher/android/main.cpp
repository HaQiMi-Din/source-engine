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
// 这里在 launcher 的每个关键步骤追加写 /storage/emulated/0/srceng/launcher.log，
// 崩溃时用户直接读该文件即可判断崩在哪一步（若文件根本没生成=launcher.so 都没加载成功）。
void ModHub_TraceLog( const char *fmt, ... )
{
	static char path[1024];
	const char *appdata = getenv( "APP_DATA_PATH" );
	if ( !appdata || !*appdata )
		appdata = "/storage/emulated/0/srceng";
	snprintf( path, sizeof path, "%s/launcher.log", appdata );

	FILE *f = fopen( path, "a" );
	if ( !f )
		return;
	va_list ap;
	va_start( ap, fmt );
	vfprintf( f, fmt, ap );
	va_end( ap );
	fprintf( f, "\n" );
	fclose( f );
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
