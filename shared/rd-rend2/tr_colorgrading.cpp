/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Color grading with 3D LUTs, applied by the shared output transform
// (glsl/output_transform.glsl) after tone mapping and sRGB encoding. LUTs
// map display-encoded sRGB values to display-encoded sRGB values.
//
// The LUT in use is, in order:
//  - r_colorGradingLUT, when set (a .cube file, or *identity)
//  - maps/<map>.cube next to the .bsp of the current map, if it exists
//  - none

#include "tr_local.h"
#include "tr_cube_lut.h"

#define IDENTITY_LUT_NAME "*identity"
#define IDENTITY_LUT_SIZE 33

static image_t *R_LoadColorGradingLUT( const char *name )
{
	char imageName[MAX_QPATH];
	if ( strlen(name) + strlen("*lut/") >= sizeof(imageName) )
	{
		ri.Printf(PRINT_WARNING, "WARNING: color grading LUT name '%s' is too long\n", name);
		return nullptr;
	}
	Com_sprintf(imageName, sizeof(imageName), "*lut/%s", name);

	image_t *image = R_GetLoadedImage(imageName, IMGFLAG_3D);
	if ( image )
	{
		return image;
	}

	std::vector<uint16_t> texels;
	int size = IDENTITY_LUT_SIZE;
	if ( !Q_stricmp(name, IDENTITY_LUT_NAME) )
	{
		R_MakeIdentityCubeLUT(size, texels);
	}
	else
	{
		char *buffer = nullptr;
		ri.FS_ReadFile(name, (void **)&buffer);
		if ( !buffer )
		{
			ri.Printf(PRINT_WARNING, "WARNING: couldn't load color grading LUT '%s'\n", name);
			return nullptr;
		}

		const char *error = R_ParseCubeLUT(buffer, &size, texels);
		ri.FS_FreeFile(buffer);

		if ( error )
		{
			ri.Printf(PRINT_WARNING, "WARNING: color grading LUT '%s': %s\n", name, error);
			return nullptr;
		}
	}

	return R_CreateImage3D(imageName, (byte *)texels.data(), size, size, size, GL_RGBA16);
}

/*
===============
R_CreateColorGradingImages

The identity LUT is bound whenever no LUT is used, so the sampler always
has a complete texture. 2x2x2 is an exact identity with linear filtering.
===============
*/
void R_CreateColorGradingImages( void )
{
	std::vector<uint16_t> texels;
	R_MakeIdentityCubeLUT(2, texels);
	tr.identityLutImage = R_CreateImage3D("*identityLut", (byte *)texels.data(), 2, 2, 2, GL_RGBA16);
}

/*
===============
R_SetMapColorGrading

Looks for maps/<map>.cube next to the .bsp of the map being loaded.
===============
*/
void R_SetMapColorGrading( const char *worldName )
{
	char path[MAX_QPATH];

	tr.mapColorGradingLut[0] = '\0';
	Com_sprintf(path, sizeof(path), "%s.cube", worldName);
	if ( ri.FS_ReadFile(path, nullptr) > 0 )
	{
		Q_strncpyz(tr.mapColorGradingLut, path, sizeof(tr.mapColorGradingLut));
	}
}

/*
===============
R_UpdateColorGrading

Called once per scene. Loads the LUT when the selection changes, so
r_colorGradingLUT takes effect immediately.
===============
*/
void R_UpdateColorGrading( void )
{
	const char *name = "";
	if ( r_colorGrading->integer )
	{
		name = r_colorGradingLUT->string[0] ? r_colorGradingLUT->string : tr.mapColorGradingLut;
	}

	if ( !strcmp(name, tr.colorGradingLutName) )
	{
		return;
	}

	// Remember failures as well, so they are only reported once
	Q_strncpyz(tr.colorGradingLutName, name, sizeof(tr.colorGradingLutName));
	tr.colorGradingLutImage = name[0] ? R_LoadColorGradingLUT(name) : nullptr;

	if ( tr.colorGradingLutImage )
	{
		ri.Printf(PRINT_DEVELOPER, "Color grading LUT: %s (%d^3)\n", name, tr.colorGradingLutImage->width);
	}
}
