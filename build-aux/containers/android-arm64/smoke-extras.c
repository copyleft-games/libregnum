/* smoke-extras.c - android-arm64 image smoke test for the non-GLib libraries
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * setup.sh links this program (as an executable and as a shared object,
 * the way the game ships as libmain.so) against the static LuaJIT, bzip2
 * and libjpeg-turbo in /opt/android/arm64-v8a, through their .pc files,
 * so an archive that is missing, built for the wrong ABI, not
 * position-independent or described by a wrong .pc file fails the image
 * build.  An Android binary cannot run here; linking is the test.  Built
 * gnu89 with the project's warning flags.
 */

#include <stdio.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <bzlib.h>
#include <jpeglib.h>

int
smoke_extras(void);

int
smoke_extras(void)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	lua_State *state;
	lua_Number answer;

	/* LuaJIT: a state with the standard libraries runs a chunk */
	state = luaL_newstate();
	if (state == NULL)
		return 1;
	luaL_openlibs(state);
	if (luaL_dostring(state, "return 6 * 7") != 0)
	{
		lua_close(state);
		return 1;
	}
	answer = lua_tonumber(state, -1);
	lua_close(state);

	/* libjpeg: an encoder object is created and destroyed */
	memset(&cinfo, 0, sizeof(cinfo));
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	/* bzip2: the library reports its version */
	printf("smoke: LuaJIT %g, bzip2 %s, libjpeg %d\n",
	       (double)answer, BZ2_bzlibVersion(), JPEG_LIB_VERSION);
	return answer == (lua_Number)42 ? 0 : 1;
}

#ifndef SMOKE_SHARED
int
main(
	int     argc,
	char  **argv
){
	(void)argc;
	(void)argv;

	return smoke_extras();
}
#endif
