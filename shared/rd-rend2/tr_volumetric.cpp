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

// Froxel volumetric fog (r_volumetricFog 2), see docs/rend2-volumetric-fog.md.
//
// r_volumetricFog 1 (legacy) ray marches the static light grid per fogged
// surface. Mode 2 keeps the same media (the BSP fog volumes, their colors and
// depthForOpaque) but lights it with a camera aligned froxel volume:
//
//   inject     per froxel: extinction of the fog volumes containing it and
//              the light scattered towards the camera: baked light grid
//              without the sun, the sun (cascaded shadow maps) and the
//              dynamic lights (their shadow maps), Henyey-Greenstein phase.
//              Temporally filtered with the reprojected previous volume.
//   integrate  front to back along every froxel column: in-scattering and
//              transmittance up to the far side of each slice (Beer-Lambert,
//              the same discretisation as the legacy ray march).
//   composite  one full screen pass after the opaque layers (sort <= SS_FOG):
//              scene * transmittance + in-scattering at the depth buffer.
//              Transparent surfaces keep their fog pass / in-shader fog, with
//              the volume looked up at the fragment instead of ray marched.
//
// GL 3.2 has no compute shaders: every slice of a 3D texture is rendered
// with a full screen triangle into a framebuffer with that layer attached.
//
// Only the main view of the first world scene of a frame uses the volume;
// portals, mirrors, sky portals, the LA goggles and other scenes use the
// legacy fog path.

#include "tr_local.h"

#include <algorithm>

#define FROXEL_NEAR 8.0f
#define FROXEL_AUTO_FAR 4096.0f

// camera changes between two frames that invalidate the history
#define FROXEL_CUT_DISTANCE 256.0f
#define FROXEL_CUT_COS_ANGLE 0.2588f	// 75 degrees
#define FROXEL_CUT_FOV 0.15f			// relative

// light grid cells whose light comes from within ~10 degrees of the sun
// direction are sun, beyond ~25 degrees not
#define FROXEL_SUN_COS_OUTER 0.9063f	// cos(25)
#define FROXEL_SUN_COS_INNER 0.9848f	// cos(10)

// r_volumetricFogQuality 0, 1, 2: screen pixels per froxel and depth slices.
// Starting points, see docs/rend2-volumetric-fog.md (profiling).
static const int froxelQualityGridScale[] = { 16, 8, 8 };
static const int froxelQualitySlices[] = { 32, 48, 64 };

struct froxelState_t
{
	qboolean resources;
	int width, height, depth;

	// the volume of this frame
	qboolean frameActive;		// injected and integrated this frame
	qboolean frameUsable;		// lookups allowed this frame (built or frozen)
	int frameScene;				// scene of the frame that owns the volume
	int builtFrameNumber;
	qboolean built;				// GPU passes of this frame ran
	int current;				// froxelInjectImage written this frame
	int lightMask[FROXEL_MAX_SLICES];
	qboolean frameHeightFog;	// the volume of this frame has media outside the BSP fog
								// volumes: the height fog or local fog volumes

	// the last froxelInjectImage the GPU passes actually wrote: the history
	// must not be an image whose build was skipped (never initialized or stale)
	int builtVolumeFrame;
	int builtVolumeImage;

	// the camera of the volume in froxelInjectImage[current]
	qboolean hasVolume;
	int volumeFrameNumber;
	const world_t *world;
	matrix_t viewProjection;
	vec3_t origin;
	vec3_t forward;
	float fovX, fovY;
	float nearZ, farZ;
	int debug;
	unsigned int noiseKey;
	unsigned int frameIndex;

	// frozen froxel camera (r_volumetricFogFreeze)
	qboolean frozen;
	VolumetricFogBlock frozenBlock;
};

static froxelState_t s_vf;

qboolean R_VolumetricFroxelEnabled( void )
{
	return s_vf.resources;
}

/*
============================================================

Density noise (r_volumetricFogNoise)

A tiling 64^3 RGBA8 texture generated at renderer init, sampled in world
space: r = macro field, g = detail field (independent), b and a unused. Each
field is a tileable gradient noise FBM (lattice periods 4, 8 and 16 cells per
tile, weights 1, 0.5, 0.25, every octave shifted by its own fraction of a cell
so that their lattice points, where gradient noise is 0, do not line up into
a visible grid), histogram equalized so that its texels are uniformly
distributed over 0..255 (mean exactly 0.5). The density modulation

  f(n; c) = (1 + c) * n^c

then has a mean of exactly 1 for any contrast c; the remaining deviation of
the filtered texture (trilinear, mips) is measured per mip level on the CPU
and divided out (noiseNorm tables, every half mip level).

============================================================
*/

#define FROXEL_NOISE_SIZE 64
#define FROXEL_NOISE_LEVELS 7		// 64, 32, ..., 1
#define FROXEL_NOISE_TEXELS (FROXEL_NOISE_SIZE * FROXEL_NOISE_SIZE * FROXEL_NOISE_SIZE)
#define FROXEL_NOISE_MEAN_SAMPLES 32768
// all mip levels of one field: 64^3 + 32^3 + ... + 1
#define FROXEL_NOISE_CHAIN (262144 + 32768 + 4096 + 512 + 64 + 8 + 1)

struct froxelNoise_t
{
	qboolean valid;
	// CPU copy of both fields with their box filtered mips (as the GPU mips),
	// static: kept over renderer restarts
	byte chain[2][FROXEL_NOISE_CHAIN];
	byte *levels[2][FROXEL_NOISE_LEVELS];

	// mean normalization at lod 0, 0.5, ..., 6 (13..15 = lod 6), cached by contrast
	float normContrast[2];
	float norm[2][16];
};

static froxelNoise_t s_noise;

static uint32_t R_NoiseHash( uint32_t x )
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static const float noiseGradients[12][3] =
{
	{ 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
	{ 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
	{ 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 },
};

// gradient index of every lattice point of an octave (period^3 <= 16^3)
static void R_NoiseLatticeGradients( byte *gradients, int period, uint32_t seed )
{
	for ( int iz = 0; iz < period; iz++ )
		for ( int iy = 0; iy < period; iy++ )
			for ( int ix = 0; ix < period; ix++ )
			{
				const uint32_t h = R_NoiseHash(seed ^ R_NoiseHash((uint32_t)ix + R_NoiseHash((uint32_t)iy + R_NoiseHash((uint32_t)iz))));
				gradients[(iz * period + iy) * period + ix] = (byte)(h % 12);
			}
}

static float R_NoiseFade( float t )
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// tileable gradient noise, x, y, z in lattice cells; the lattice repeats
// every period (power of two) cells, so the tile wraps seamlessly
static float R_NoisePerlin( float x, float y, float z, int period, const byte *gradients )
{
	const int ix = (int)floorf(x);
	const int iy = (int)floorf(y);
	const int iz = (int)floorf(z);
	const float fx = x - (float)ix;
	const float fy = y - (float)iy;
	const float fz = z - (float)iz;
	const float u = R_NoiseFade(fx);
	const float v = R_NoiseFade(fy);
	const float w = R_NoiseFade(fz);

	float corners[8];
	for ( int c = 0; c < 8; c++ )
	{
		const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
		const int mask = period - 1;
		const float *g = noiseGradients[gradients[
			(((iz + dz) & mask) * period + ((iy + dy) & mask)) * period + ((ix + dx) & mask)]];
		corners[c] = g[0] * (fx - (float)dx) + g[1] * (fy - (float)dy) + g[2] * (fz - (float)dz);
	}

	const float x00 = corners[0] + u * (corners[1] - corners[0]);
	const float x10 = corners[2] + u * (corners[3] - corners[2]);
	const float x01 = corners[4] + u * (corners[5] - corners[4]);
	const float x11 = corners[6] + u * (corners[7] - corners[6]);
	const float y0 = x00 + v * (x10 - x00);
	const float y1 = x01 + v * (x11 - x01);
	return y0 + w * (y1 - y0);
}

struct noiseRank_t
{
	float value;
	int index;
	bool operator<( const noiseRank_t& other ) const
	{
		return (value != other.value) ? (value < other.value) : (index < other.index);
	}
};

// one field: FBM of 3 octaves, histogram equalized to 0..255
static void R_NoiseGenerateField( byte *out, uint32_t seed )
{
	static const int periods[3] = { 4, 8, 16 };
	static const float weights[3] = { 1.0f, 0.5f, 0.25f };
	// in cells of each octave: the tile stays periodic
	static const float shifts[3][3] = {
		{ 0.0f, 0.0f, 0.0f }, { 0.371f, 0.683f, 0.529f }, { 0.817f, 0.243f, 0.461f } };
	const int n = FROXEL_NOISE_SIZE;

	static byte gradients[3][16 * 16 * 16];
	for ( int o = 0; o < 3; o++ )
		R_NoiseLatticeGradients(gradients[o], periods[o], seed + 0x632be5abu * (uint32_t)o);

	noiseRank_t *ranks = (noiseRank_t *)Z_Malloc(FROXEL_NOISE_TEXELS * sizeof(noiseRank_t), TAG_TEMP_WORKSPACE, qfalse);
	for ( int k = 0; k < n; k++ )
	{
		for ( int j = 0; j < n; j++ )
		{
			for ( int i = 0; i < n; i++ )
			{
				float value = 0.0f;
				for ( int o = 0; o < 3; o++ )
				{
					const float scale = (float)periods[o] / (float)n;
					value += weights[o] * R_NoisePerlin(
						((float)i + 0.5f) * scale + shifts[o][0],
						((float)j + 0.5f) * scale + shifts[o][1],
						((float)k + 0.5f) * scale + shifts[o][2],
						periods[o], gradients[o]);
				}
				const int index = (k * n + j) * n + i;
				ranks[index].value = value;
				ranks[index].index = index;
			}
		}
	}

	// rank -> 0..255, every value is taken by exactly 1 / 256 of the texels
	std::sort(ranks, ranks + FROXEL_NOISE_TEXELS);
	for ( int r = 0; r < FROXEL_NOISE_TEXELS; r++ )
		out[ranks[r].index] = (byte)((r * 256) / FROXEL_NOISE_TEXELS);

	Z_Free(ranks);
}

// 2x2x2 box filter with rounding (glGenerateMipmap of an RGBA8 texture)
static void R_NoiseDownsample( const byte *in, int size, byte *out )
{
	const int half = size / 2;
	for ( int k = 0; k < half; k++ )
	{
		for ( int j = 0; j < half; j++ )
		{
			for ( int i = 0; i < half; i++ )
			{
				int sum = 0;
				for ( int c = 0; c < 8; c++ )
				{
					const int x = 2 * i + (c & 1), y = 2 * j + ((c >> 1) & 1), z = 2 * k + ((c >> 2) & 1);
					sum += in[(z * size + y) * size + x];
				}
				out[(k * half + j) * half + i] = (byte)((sum + 4) / 8);
			}
		}
	}
}

// trilinear, repeat, level of the given size, u in tile units; 0..1
static float R_NoiseSample( const byte *level, int size, const float *u )
{
	int i0[3], i1[3];
	float f[3];
	for ( int a = 0; a < 3; a++ )
	{
		const float x = u[a] * (float)size - 0.5f;
		const float fl = floorf(x);
		f[a] = x - fl;
		i0[a] = (int)fl & (size - 1);	// sizes are powers of two
		i1[a] = (i0[a] + 1) & (size - 1);
	}

	float result = 0.0f;
	for ( int c = 0; c < 8; c++ )
	{
		const int x = (c & 1) ? i1[0] : i0[0];
		const int y = (c & 2) ? i1[1] : i0[1];
		const int z = (c & 4) ? i1[2] : i0[2];
		const float w = ((c & 1) ? f[0] : 1.0f - f[0]) * ((c & 2) ? f[1] : 1.0f - f[1]) * ((c & 4) ? f[2] : 1.0f - f[2]);
		result += w * (float)level[(z * size + y) * size + x];
	}
	return result / 255.0f;
}

// as textureLod: fractional lods blend the two levels
static float R_NoiseSampleLod( int field, const float *u, float lod )
{
	lod = Com_Clamp(0.0f, (float)(FROXEL_NOISE_LEVELS - 1), lod);
	const int l = (int)lod;
	const float a = R_NoiseSample(s_noise.levels[field][l], FROXEL_NOISE_SIZE >> l, u);
	if ( l >= FROXEL_NOISE_LEVELS - 1 )
		return a;
	const float b = R_NoiseSample(s_noise.levels[field][l + 1], FROXEL_NOISE_SIZE >> (l + 1), u);
	return a + (b - a) * (lod - (float)l);
}

static float R_NoiseContrast( float n, float c )
{
	return (1.0f + c) * powf(MAX(n, 1e-4f), c);
}

// 1 / E[f(n; c)] at lod 0, 0.5, ..., 6 of a field, n filtered as the GPU does
// (trilinear, mip blend) at low discrepancy (R3 sequence) positions. Blending
// two levels lowers the variance of n, so the half levels are measured too.
static void R_NoiseMeasureNorm( int field, float contrast, float *norm )
{
	for ( int j = 0; j < 16; j++ )
		norm[j] = 1.0f;
	if ( contrast <= 0.0f )
		return;

	static const double alpha[3] = { 0.8191725133961645, 0.6710436067037893, 0.5497004779019703 };
	const int numSteps = 2 * (FROXEL_NOISE_LEVELS - 1) + 1;
	for ( int j = 0; j < numSteps; j++ )
	{
		double sum = 0.0;
		for ( int s = 0; s < FROXEL_NOISE_MEAN_SAMPLES; s++ )
		{
			float u[3];
			for ( int a = 0; a < 3; a++ )
			{
				const double x = 0.5 + alpha[a] * (double)s;
				u[a] = (float)(x - floor(x));
			}
			sum += R_NoiseContrast(R_NoiseSampleLod(field, u, 0.5f * (float)j), contrast);
		}
		const double mean = sum / (double)FROXEL_NOISE_MEAN_SAMPLES;
		norm[j] = (mean > 1e-6) ? (float)(1.0 / mean) : 1.0f;
	}
	for ( int j = numSteps; j < 16; j++ )
		norm[j] = norm[numSteps - 1];
}

static void R_CreateVolumetricNoiseImage( void )
{
	static const uint32_t seeds[2] = { 0x5f3759dfu, 0x9e3779b9u };
	const int start = ri.Milliseconds();

	if ( !s_noise.valid )
	{
		for ( int field = 0; field < 2; field++ )
		{
			int offset = 0;
			for ( int l = 0; l < FROXEL_NOISE_LEVELS; l++ )
			{
				const int size = FROXEL_NOISE_SIZE >> l;
				s_noise.levels[field][l] = s_noise.chain[field] + offset;
				offset += size * size * size;
			}

			R_NoiseGenerateField(s_noise.levels[field][0], seeds[field]);
			for ( int l = 1; l < FROXEL_NOISE_LEVELS; l++ )
			{
				R_NoiseDownsample(s_noise.levels[field][l - 1], FROXEL_NOISE_SIZE >> (l - 1),
					s_noise.levels[field][l]);
			}
		}
		s_noise.valid = qtrue;
	}
	s_noise.normContrast[0] = s_noise.normContrast[1] = -1.0f;

	byte *texels = (byte *)Z_Malloc(FROXEL_NOISE_TEXELS * 4, TAG_TEMP_WORKSPACE, qtrue);
	for ( int t = 0; t < FROXEL_NOISE_TEXELS; t++ )
	{
		texels[t * 4 + 0] = s_noise.levels[0][0][t];
		texels[t * 4 + 1] = s_noise.levels[1][0][t];
	}
	// repeat, trilinear, full mip chain
	tr.froxelNoiseImage = R_CreateImage3D("*froxelNoise", texels,
		FROXEL_NOISE_SIZE, FROXEL_NOISE_SIZE, FROXEL_NOISE_SIZE, GL_RGBA8, IMGFLAG_MIPMAP);
	Z_Free(texels);

	ri.Printf(PRINT_DEVELOPER, "Froxel fog density noise: %d^3 RGBA8, %d ms\n",
		FROXEL_NOISE_SIZE, ri.Milliseconds() - start);
}

// the mean normalization of both fields for the current contrasts
static const float *R_VolumetricNoiseNorm( int field, float contrast )
{
	if ( s_noise.normContrast[field] != contrast )
	{
		R_NoiseMeasureNorm(field, contrast, s_noise.norm[field]);
		s_noise.normContrast[field] = contrast;
		ri.Printf(PRINT_DEVELOPER, "Froxel fog noise %s, contrast %.2f: mean normalization %.4f %.4f %.4f %.4f %.4f %.4f %.4f (lod 0..6)\n",
			field ? "detail" : "macro", contrast,
			s_noise.norm[field][0], s_noise.norm[field][2], s_noise.norm[field][4], s_noise.norm[field][6],
			s_noise.norm[field][8], s_noise.norm[field][10], s_noise.norm[field][12]);
	}
	return s_noise.norm[field];
}

/*
============================================================

Resources

============================================================
*/

void R_CreateVolumetricImages( int width, int height )
{
	Com_Memset(&s_vf, 0, sizeof(s_vf));
	s_vf.builtVolumeFrame = -1;
	s_vf.builtVolumeImage = -1;
	tr.froxelInjectImage[0] = tr.froxelInjectImage[1] = NULL;
	tr.froxelDynamicImage = NULL;
	tr.froxelIntegratedImage = NULL;
	tr.froxelCarryImage[0] = tr.froxelCarryImage[1] = NULL;
	tr.froxelTailImage = NULL;
	tr.froxelNoiseImage = NULL;

	if ( r_volumetricFog->integer != 2 )
		return;

	// the VolumetricFog block (with the local fog volumes) must fit in a UBO;
	// GL 3.2 guarantees 16 KB, the block is below that
	if ( glRefConfig.maxUniformBlockSize > 0 &&
		(size_t)glRefConfig.maxUniformBlockSize < sizeof(VolumetricFogBlock) )
	{
		ri.Printf(PRINT_WARNING, "r_volumetricFog 2: uniform blocks up to %d bytes, %d needed: froxel fog disabled\n",
			glRefConfig.maxUniformBlockSize, (int)sizeof(VolumetricFogBlock));
		return;
	}

	const int quality = Com_Clampi(0, 2, r_volumetricFogQuality->integer);
	const int gridScale = r_volumetricFogGridScale->integer > 0 ?
		Com_Clampi(4, 32, r_volumetricFogGridScale->integer) : froxelQualityGridScale[quality];
	const int slices = r_volumetricFogSlices->integer > 0 ?
		Com_Clampi(16, FROXEL_MAX_SLICES, r_volumetricFogSlices->integer) : froxelQualitySlices[quality];

	s_vf.width = Q_max(1, (width + gridScale - 1) / gridScale);
	s_vf.height = Q_max(1, (height + gridScale - 1) / gridScale);
	s_vf.depth = slices;

	for ( int i = 0; i < 2; i++ )
	{
		tr.froxelInjectImage[i] = R_CreateImage3D(
			va("*froxelInject%d", i), NULL, s_vf.width, s_vf.height, s_vf.depth, GL_RGBA16F);
		tr.froxelCarryImage[i] = R_CreateImage(
			va("*froxelCarry%d", i), NULL, s_vf.width, s_vf.height, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
	}

	tr.froxelDynamicImage = R_CreateImage3D(
		"*froxelDynamic", NULL, s_vf.width, s_vf.height, s_vf.depth, GL_R11F_G11F_B10F);
	tr.froxelIntegratedImage = R_CreateImage3D(
		"*froxelIntegrated", NULL, s_vf.width, s_vf.height, s_vf.depth, GL_RGBA16F);
	tr.froxelTailImage = R_CreateImage(
		"*froxelTail", NULL, s_vf.width, s_vf.height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);

	R_CreateVolumetricNoiseImage();

	s_vf.resources = qtrue;

	if ( !r_depthPrepass->integer )
		ri.Printf(PRINT_WARNING, "r_volumetricFog 2 needs r_depthPrepass 1, the legacy volumetric fog is used\n");

	ri.Printf(PRINT_ALL, "Froxel volumetric fog: %d x %d x %d froxels (%d pixels per froxel)\n",
		s_vf.width, s_vf.height, s_vf.depth, gridScale);
}

void R_CreateVolumetricFBOs( void )
{
	tr.froxelInjectFbo = NULL;
	tr.froxelIntegrateFbo = NULL;
	tr.froxelCompositeFbo = NULL;

	if ( !s_vf.resources )
		return;

	// injection: one layer of froxelInjectImage[current] and of the dynamic
	// light volume, attached per slice
	tr.froxelInjectFbo = FBO_Create("_froxelInject", s_vf.width, s_vf.height);
	FBO_Bind(tr.froxelInjectFbo);
	qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		tr.froxelInjectImage[0]->texnum, 0, 0);
	qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
		tr.froxelDynamicImage->texnum, 0, 0);
	glState.currentFBO->colorImage[0] = tr.froxelInjectImage[0];
	glState.currentFBO->colorBuffers[0] = tr.froxelInjectImage[0]->texnum;
	glState.currentFBO->colorImage[1] = tr.froxelDynamicImage;
	glState.currentFBO->colorBuffers[1] = tr.froxelDynamicImage->texnum;
	{
		const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		qglDrawBuffers(2, bufs);
	}
	R_CheckFBO(tr.froxelInjectFbo);

	// integration: a layer of the integrated volume, the carried state and
	// (last slice) the tail
	tr.froxelIntegrateFbo = FBO_Create("_froxelIntegrate", s_vf.width, s_vf.height);
	FBO_Bind(tr.froxelIntegrateFbo);
	qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		tr.froxelIntegratedImage->texnum, 0, 0);
	glState.currentFBO->colorImage[0] = tr.froxelIntegratedImage;
	glState.currentFBO->colorBuffers[0] = tr.froxelIntegratedImage->texnum;
	FBO_AttachTextureImage(tr.froxelCarryImage[0], 1);
	FBO_AttachTextureImage(tr.froxelTailImage, 2);
	{
		const GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
		qglDrawBuffers(3, bufs);
	}
	R_CheckFBO(tr.froxelIntegrateFbo);

	// Clear every volume once: the images are created without data, and a
	// lookup of a volume that was never built must see no fog, not garbage
	// (NaN would be fed back by the temporal filter forever).
	{
		const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const float noFog[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		// clears obey the color masks: draw buffer 2 (the tail) is masked by
		// default with SSR / SSGI, and a map change keeps the context (glState is
		// reset, the GL masks are not), so set every mask explicitly
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glState.screenAuxWrite = true;

		GL_SetViewportAndScissor(0, 0, s_vf.width, s_vf.height);

		FBO_Bind(tr.froxelInjectFbo);
		for ( int k = 0; k < s_vf.depth; k++ )
		{
			for ( int i = 0; i < 2; i++ )
			{
				qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					tr.froxelInjectImage[i]->texnum, 0, k);
				qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
					tr.froxelDynamicImage->texnum, 0, k);
				qglClearBufferfv(GL_COLOR, 0, zero);
				qglClearBufferfv(GL_COLOR, 1, zero);
			}
		}
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			tr.froxelInjectImage[0]->texnum, 0, 0);
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
			tr.froxelDynamicImage->texnum, 0, 0);

		FBO_Bind(tr.froxelIntegrateFbo);
		for ( int k = 0; k < s_vf.depth; k++ )
		{
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				tr.froxelIntegratedImage->texnum, 0, k);
			qglClearBufferfv(GL_COLOR, 0, noFog);
		}
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			tr.froxelIntegratedImage->texnum, 0, 0);
		for ( int i = 0; i < 2; i++ )
		{
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
				GL_TEXTURE_2D, tr.froxelCarryImage[i]->texnum, 0);
			qglClearBufferfv(GL_COLOR, 1, noFog);
		}
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
			GL_TEXTURE_2D, tr.froxelCarryImage[0]->texnum, 0);
		qglClearBufferfv(GL_COLOR, 2, zero);	// tail: no medium beyond far

		GL_ResetScreenAuxWrite();
	}

	// composite: color and glow of renderFbo only, the sampled depth must not
	// be attached
	tr.froxelCompositeFbo = FBO_Create("_froxelComposite", tr.renderFbo->width, tr.renderFbo->height);
	FBO_Bind(tr.froxelCompositeFbo);
	if ( tr.msaaResolveFbo )
	{
		for ( int i = 0; i < 2; i++ )
		{
			qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
				GL_RENDERBUFFER, tr.renderFbo->colorBuffers[i]);
			glState.currentFBO->colorBuffers[i] = tr.renderFbo->colorBuffers[i];
		}
	}
	else
	{
		FBO_AttachTextureImage(tr.renderImage, 0);
		FBO_AttachTextureImage(tr.glowImage, 1);
	}
	{
		const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		qglDrawBuffers(2, bufs);
	}
	R_CheckFBO(tr.froxelCompositeFbo);
}

/*
============================================================

Light grid split by the sun direction

============================================================
*/

static float R_VolumetricSRGBToLinear( float c )
{
	return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float R_VolumetricSmoothstep( float edge0, float edge1, float x )
{
	const float t = Com_Clamp(0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
	return t * t * (3.0f - 2.0f * t);
}

static int R_VolumetricCompareFloats( const void *a, const void *b )
{
	const float fa = *(const float *)a;
	const float fb = *(const float *)b;
	return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

/*
=================
R_BuildVolumetricLightGrid

The legacy volumetric light map merges the ambient and directed light of
every light grid cell (volumetricLightMaps[0], R_BuildLightGridTexture). The
directed part contains the baked sun, which the froxel fog lights in real
time with the cascaded shadow maps. Split the merged value in two textures
with the same layout, so that static + sun == the legacy value:

  sun    = the directed light of cells lit from the sun direction
  static = everything else (ambient, other lights)

The realtime sun radiance is estimated from the sunlit cells, so the beams
have the brightness the map was compiled with.
=================
*/
void R_BuildVolumetricLightGrid( world_t *world )
{
	world->volumetricStaticGrid = NULL;
	world->volumetricSunGrid = NULL;
	world->volumetricHasSunCells = qfalse;
	VectorClear(world->volumetricSunRadiance);

	if ( r_volumetricFog->integer != 2 || !world->lightGridData || world->numGridArrayElements <= 0 )
		return;

	const int numCells = world->numGridArrayElements;
	if ( numCells != world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] )
	{
		ri.Printf(PRINT_WARNING, "R_BuildVolumetricLightGrid: light grid size mismatch, no sun split\n");
	}

	const qboolean splitSun = tr.sunParsed;
	vec3_t sunDir;
	VectorCopy(tr.sunDirection, sunDir);
	VectorNormalize(sunDir);

	uint16_t *staticData = (uint16_t *)Z_Malloc(numCells * sizeof(uint16_t) * 4, TAG_TEMP_WORKSPACE, qtrue);
	uint16_t *sunData = (uint16_t *)Z_Malloc(numCells * sizeof(uint16_t) * 4, TAG_TEMP_WORKSPACE, qtrue);
	float *sunLuma = (float *)Z_Malloc(numCells * sizeof(float), TAG_TEMP_WORKSPACE, qtrue);
	int numSunCells = 0;
	vec3_t sunColorSum = { 0.0f, 0.0f, 0.0f };

	for ( int i = 0; i < numCells; i++ )
	{
		const mgrid_t *data = world->lightGridData + world->lightGridArray[i];
		vec3_t ambient, direct, total;

		if ( world->hdrLightGrid )
		{
			const float *hdrData = world->hdrLightGrid + (i * 6);
			for ( int c = 0; c < 3; c++ )
			{
				ambient[c] = hdrData[c];
				direct[c] = hdrData[c + 3];
				total[c] = ambient[c] + direct[c];
			}
		}
		else
		{
			for ( int c = 0; c < 3; c++ )
			{
				ambient[c] = data->ambientLight[0][c] / 255.0f;
				direct[c] = data->directLight[0][c] / 255.0f;
				if ( tr.forcedLinearLight )
				{
					// the legacy texture is GL_SRGB8 then
					ambient[c] = R_VolumetricSRGBToLinear(ambient[c]);
					direct[c] = R_VolumetricSRGBToLinear(direct[c]);
				}
				total[c] = MAX(ambient[c], direct[c]);
			}
		}

		float sunFraction = 0.0f;
		if ( splitSun )
		{
			// direction towards the light, as R_SetupEntityLightingGrid
			const float lat = data->latLong[1] * (2.0f * M_PI / 255.0f);
			const float lng = data->latLong[0] * (2.0f * M_PI / 255.0f);
			vec3_t cellDir;
			cellDir[0] = cosf(lat) * sinf(lng);
			cellDir[1] = sinf(lat) * sinf(lng);
			cellDir[2] = cosf(lng);
			sunFraction = R_VolumetricSmoothstep(
				FROXEL_SUN_COS_OUTER, FROXEL_SUN_COS_INNER, DotProduct(cellDir, sunDir));
		}

		vec3_t sun;
		for ( int c = 0; c < 3; c++ )
			sun[c] = MIN(sunFraction * direct[c], total[c]);

		staticData[i * 4 + 0] = FloatToHalf(total[0] - sun[0]);
		staticData[i * 4 + 1] = FloatToHalf(total[1] - sun[1]);
		staticData[i * 4 + 2] = FloatToHalf(total[2] - sun[2]);
		staticData[i * 4 + 3] = FloatToHalf(sunFraction);

		sunData[i * 4 + 0] = FloatToHalf(sun[0]);
		sunData[i * 4 + 1] = FloatToHalf(sun[1]);
		sunData[i * 4 + 2] = FloatToHalf(sun[2]);
		sunData[i * 4 + 3] = FloatToHalf(1.0f);

		const float luma = 0.2126f * sun[0] + 0.7152f * sun[1] + 0.0722f * sun[2];
		if ( sunFraction > 0.5f && luma > 0.0f )
		{
			sunLuma[numSunCells++] = luma;
			VectorAdd(sunColorSum, sun, sunColorSum);
		}
	}

	world->volumetricStaticGrid = R_CreateImage3D(
		"*volumetricStaticGrid", (byte *)staticData,
		world->lightGridBounds[0], world->lightGridBounds[1], world->lightGridBounds[2],
		GL_RGBA16F);
	world->volumetricSunGrid = R_CreateImage3D(
		"*volumetricSunGrid", (byte *)sunData,
		world->lightGridBounds[0], world->lightGridBounds[1], world->lightGridBounds[2],
		GL_RGBA16F);

	// realtime sun radiance: 90th percentile of the sunlit cells, with their
	// average color. A handful of cells is not a sun.
	if ( numSunCells >= 16 )
	{
		qsort(sunLuma, numSunCells, sizeof(float), R_VolumetricCompareFloats);
		const float percentile = sunLuma[(numSunCells * 9) / 10];
		const float sumLuma = 0.2126f * sunColorSum[0] + 0.7152f * sunColorSum[1] + 0.0722f * sunColorSum[2];
		if ( sumLuma > 0.0f )
		{
			VectorScale(sunColorSum, percentile / sumLuma, world->volumetricSunRadiance);
			world->volumetricHasSunCells = qtrue;
		}
	}

	ri.Printf(PRINT_DEVELOPER, "Froxel fog light grid: %d cells, %d sunlit, sun radiance %.3f %.3f %.3f\n",
		numCells, numSunCells, world->volumetricSunRadiance[0],
		world->volumetricSunRadiance[1], world->volumetricSunRadiance[2]);

	Z_Free(sunLuma);
	Z_Free(sunData);
	Z_Free(staticData);
}

/*
============================================================

Per frame constants (front end, RB_UpdateConstants)

============================================================
*/

// generic 4x4 inverse (column major), false if singular
static qboolean R_VolumetricInvertMatrix( const float *m, float *out )
{
	float inv[16];
	inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
	inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
	inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
	inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
	inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
	inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

	const float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
	if ( fabsf(det) < 1e-30f )
		return qfalse;

	const float invDet = 1.0f / det;
	for ( int i = 0; i < 16; i++ )
		out[i] = inv[i] * invDet;
	return qtrue;
}

static float R_VolumetricHalton( unsigned int index, unsigned int base )
{
	float result = 0.0f;
	float f = 1.0f;
	while ( index > 0 )
	{
		f /= (float)base;
		result += f * (float)(index % base);
		index /= base;
	}
	return result;
}

// view distance of the near side of slice k (slice 0 starts at the camera)
static float R_VolumetricSliceDistance( int k, float nearZ, float farZ, int numSlices )
{
	if ( k <= 0 )
		return 0.0f;
	return nearZ * powf(farZ / nearZ, (float)k / (float)numSlices);
}

// dynamic lights overlapping each slice of the main view frustum
static void R_VolumetricCullLights( const viewParms_t *view, const trRefdef_t *refdef, const vec3_t forward )
{
	// the lights of the Lights block, bit i = u_Lights[i] (Forward+: the most
	// important MAX_DLIGHTS, tr_forwardplus.cpp)
	int lightIndexes[MAX_DLIGHTS];
	int shadowLayers[MAX_DLIGHTS];
	const int numLights = R_GetUboDlights(refdef, lightIndexes, shadowLayers);
	for ( int k = 0; k < s_vf.depth; k++ )
	{
		const float sliceNear = R_VolumetricSliceDistance(k, s_vf.nearZ, s_vf.farZ, s_vf.depth);
		const float sliceFar = R_VolumetricSliceDistance(k + 1, s_vf.nearZ, s_vf.farZ, s_vf.depth);
		unsigned int mask = 0;

		for ( int i = 0; i < numLights; i++ )
		{
			const dlight_t *dl = refdef->dlights + lightIndexes[i];
			const float radius = dl->radius;
			if ( radius <= 0.0f )
				continue;

			vec3_t delta;
			VectorSubtract(dl->origin, view->ori.origin, delta);
			const float depth = DotProduct(delta, forward);
			if ( depth + radius < sliceNear || depth - radius > sliceFar )
				continue;

			qboolean inside = qtrue;
			for ( int p = 0; p < 4; p++ )
			{
				const cplane_t *plane = &view->frustum[p];
				if ( DotProduct(dl->origin, plane->normal) - plane->dist < -radius )
				{
					inside = qfalse;
					break;
				}
			}

			if ( inside )
				mask |= 1u << i;
		}

		s_vf.lightMask[k] = (int)mask;
	}
}

/*
=================
R_VolumetricHeightFog

Height fog medium (r_volumetricFogHeight 1, off by default), world anchored:

  sigma(p) = sigma0 * min(exp(-(p.z - base) / falloff), maxScale) * cutoff
  sigma0   = -ln(1.5 / 255) / r_volumetricFogHeightOpaque * volumetricFogScale

sigma0 is converted from a depthForOpaque distance exactly like the BSP fog
volumes below, so both media share one unit (extinction per world unit).
False (and a zero base extinction) when off.
=================
*/
static qboolean R_VolumetricHeightFog( vec4_t fog, vec4_t color, vec4_t top )
{
	VectorSet4(fog, 0.0f, 0.0f, 0.0f, 0.0f);
	VectorSet4(color, 0.0f, 0.0f, 0.0f, 0.0f);
	VectorSet4(top, 0.0f, 0.0f, 0.0f, 0.0f);

	const float opaque = r_volumetricFogHeightOpaque->value;
	if ( !r_volumetricFogHeight->integer || opaque <= 0.0f )
		return qfalse;

	const float extinction = (-logf(1.5f / 255.0f)) / opaque *
		tr.volumetricFogScale * r_volumetricFogScale->value;
	if ( extinction <= 0.0f )
		return qfalse;

	const float falloff = MAX(1.0f, r_volumetricFogHeightFalloff->value);
	const float maxScale = MAX(1.0f, r_volumetricFogHeightMax->value);
	VectorSet4(fog, extinction, r_volumetricFogHeightBase->value, 1.0f / falloff, logf(maxScale));

	// albedo in the fogParms convention (R_LoadFogs, ParseShader)
	vec3_t albedo = { 0.7f, 0.75f, 0.8f };
	sscanf(r_volumetricFogHeightColor->string, "%f %f %f", &albedo[0], &albedo[1], &albedo[2]);
	for ( int c = 0; c < 3; c++ )
	{
		albedo[c] = Com_Clamp(0.0f, 1.0f, albedo[c]);
		if ( tr.linearLight )
			albedo[c] = (float)sRGBtoRGB(albedo[c]);
		albedo[c] *= tr.identityLight;
	}

	// soft cutoff: fades out over the last falloff (at most the whole layer)
	const float topHeight = MAX(0.0f, r_volumetricFogHeightTop->value);
	VectorSet4(color, albedo[0], albedo[1], albedo[2], topHeight - MIN(falloff, topHeight));
	VectorSet4(top, topHeight, 0.0f, 0.0f, 0.0f);
	return qtrue;
}

/*
=================
R_VolumetricFog_f

r_vfog: adds the froxel fog medium to any map, a front end to the height
fog cvars (r_volumetricFogHeight*), so the values persist and every map
without BSP fog volumes can get its medium (and its light beams).
=================
*/
static qboolean R_VolumetricFogParseNumber( const char *s, float *out )
{
	char *end;
	const double value = strtod(s, &end);
	if ( end == s || *end != '\0' )
		return qfalse;
	*out = (float)value;
	return qtrue;
}

static void R_VolumetricFogUsage( void )
{
	ri.Printf(PRINT_ALL,
		"usage: r_vfog                     current state\n"
		"       r_vfog on | off | reset\n"
		"       r_vfog <key> <value> ...   sets the medium and switches it on\n"
		"         opaque  <units>          distance at which the fog at the base becomes opaque\n"
		"         falloff <units>          height over which the density drops by e (large = uniform)\n"
		"         color   <r> <g> <b>      scattering color 0..1\n"
		"         base    <z> | auto       base height, auto = lowest floor of the map\n"
		"         top     <units>          soft ceiling above the base, 0 = none\n"
		"         max     <scale>          density cap below the base\n"
		"       r_vfog uniform <opaque> [r g b]   uniform haze over the whole map\n"
		"example: r_vfog opaque 2500 falloff 600 color 0.75 0.8 0.85\n");
}

static void R_VolumetricFogPrint( void )
{
	ri.Printf(PRINT_ALL, "volumetric fog medium: %s\n", r_volumetricFogHeight->integer ? "on" : "off");
	ri.Printf(PRINT_ALL, "  opaque  %g\n", r_volumetricFogHeightOpaque->value);
	ri.Printf(PRINT_ALL, "  falloff %g\n", r_volumetricFogHeightFalloff->value);
	ri.Printf(PRINT_ALL, "  color   %s\n", r_volumetricFogHeightColor->string);
	ri.Printf(PRINT_ALL, "  base    %g\n", r_volumetricFogHeightBase->value);
	ri.Printf(PRINT_ALL, "  top     %g\n", r_volumetricFogHeightTop->value);
	ri.Printf(PRINT_ALL, "  max     %g\n", r_volumetricFogHeightMax->value);

	if ( r_volumetricFog->integer != 2 || !s_vf.resources )
		ri.Printf(PRINT_WARNING, "r_vfog needs r_volumetricFog 2 (then vid_restart)\n");
	else if ( !r_depthPrepass->integer )
		ri.Printf(PRINT_WARNING, "r_vfog needs r_depthPrepass 1\n");
}

void R_VolumetricFog_f( void )
{
	const int argc = ri.Cmd_Argc();
	if ( argc < 2 )
	{
		R_VolumetricFogPrint();
		ri.Printf(PRINT_ALL, "(r_vfog help for the parameters)\n");
		return;
	}

	const char *cmd = ri.Cmd_Argv(1);
	if ( !Q_stricmp(cmd, "help") || !Q_stricmp(cmd, "?") )
	{
		R_VolumetricFogUsage();
		return;
	}

	if ( !Q_stricmp(cmd, "on") || !Q_stricmp(cmd, "off") )
	{
		ri.Cvar_Set("r_volumetricFogHeight", !Q_stricmp(cmd, "on") ? "1" : "0");
		R_VolumetricFogPrint();
		return;
	}

	if ( !Q_stricmp(cmd, "reset") )
	{
		cvar_t *cvars[] = {
			r_volumetricFogHeight, r_volumetricFogHeightOpaque, r_volumetricFogHeightFalloff,
			r_volumetricFogHeightColor, r_volumetricFogHeightTop, r_volumetricFogHeightMax };
		for ( size_t i = 0; i < ARRAY_LEN(cvars); i++ )
		{
			if ( cvars[i]->resetString )
				ri.Cvar_Set(cvars[i]->name, cvars[i]->resetString);
		}
		if ( tr.world )
			R_SetHeightFogBase(tr.world);
		R_VolumetricFogPrint();
		return;
	}

	if ( !Q_stricmp(cmd, "uniform") )
	{
		float opaque;
		if ( argc < 3 || !R_VolumetricFogParseNumber(ri.Cmd_Argv(2), &opaque) )
		{
			R_VolumetricFogUsage();
			return;
		}

		ri.Cvar_Set("r_volumetricFogHeightOpaque", va("%g", opaque));
		ri.Cvar_Set("r_volumetricFogHeightFalloff", "65536");
		ri.Cvar_Set("r_volumetricFogHeightTop", "0");
		ri.Cvar_Set("r_volumetricFogHeightMax", "1");
		if ( argc >= 6 )
		{
			float rgb[3];
			for ( int c = 0; c < 3; c++ )
			{
				if ( !R_VolumetricFogParseNumber(ri.Cmd_Argv(3 + c), &rgb[c]) )
				{
					R_VolumetricFogUsage();
					return;
				}
			}
			ri.Cvar_Set("r_volumetricFogHeightColor", va("%g %g %g", rgb[0], rgb[1], rgb[2]));
		}
		ri.Cvar_Set("r_volumetricFogHeight", "1");
		R_VolumetricFogPrint();
		return;
	}

	// key value pairs: validate everything first, then apply
	struct setting_t { const char *cvar; char value[64]; };
	setting_t settings[8];
	int numSettings = 0;
	qboolean autoBase = qfalse;

	for ( int i = 1; i < argc; )
	{
		const char *key = ri.Cmd_Argv(i);
		const char *cvar = NULL;
		if ( !Q_stricmp(key, "opaque") ) cvar = "r_volumetricFogHeightOpaque";
		else if ( !Q_stricmp(key, "falloff") ) cvar = "r_volumetricFogHeightFalloff";
		else if ( !Q_stricmp(key, "base") ) cvar = "r_volumetricFogHeightBase";
		else if ( !Q_stricmp(key, "top") ) cvar = "r_volumetricFogHeightTop";
		else if ( !Q_stricmp(key, "max") ) cvar = "r_volumetricFogHeightMax";
		else if ( !Q_stricmp(key, "color") ) cvar = "r_volumetricFogHeightColor";

		if ( !cvar || numSettings >= (int)ARRAY_LEN(settings) )
		{
			ri.Printf(PRINT_WARNING, "r_vfog: unknown parameter '%s'\n", key);
			R_VolumetricFogUsage();
			return;
		}

		if ( !Q_stricmp(key, "color") )
		{
			float rgb[3];
			if ( i + 3 >= argc )	// color r g b
			{
				R_VolumetricFogUsage();
				return;
			}
			for ( int c = 0; c < 3; c++ )
			{
				if ( !R_VolumetricFogParseNumber(ri.Cmd_Argv(i + 1 + c), &rgb[c]) )
				{
					R_VolumetricFogUsage();
					return;
				}
			}
			settings[numSettings].cvar = cvar;
			Com_sprintf(settings[numSettings].value, sizeof(settings[numSettings].value),
				"%g %g %g", rgb[0], rgb[1], rgb[2]);
			numSettings++;
			i += 4;
			continue;
		}

		if ( i + 1 >= argc )
		{
			R_VolumetricFogUsage();
			return;
		}

		const char *valueString = ri.Cmd_Argv(i + 1);
		if ( !Q_stricmp(key, "base") && !Q_stricmp(valueString, "auto") )
		{
			autoBase = qtrue;
			i += 2;
			continue;
		}

		float value;
		if ( !R_VolumetricFogParseNumber(valueString, &value) )
		{
			ri.Printf(PRINT_WARNING, "r_vfog: '%s' is not a number\n", valueString);
			return;
		}
		settings[numSettings].cvar = cvar;
		Com_sprintf(settings[numSettings].value, sizeof(settings[numSettings].value), "%g", value);
		numSettings++;
		i += 2;
	}

	for ( int i = 0; i < numSettings; i++ )
		ri.Cvar_Set(settings[i].cvar, settings[i].value);
	if ( autoBase )
	{
		if ( tr.world )
			R_SetHeightFogBase(tr.world);
		else
			ri.Printf(PRINT_WARNING, "r_vfog: no map loaded, base auto is applied on the next map load\n");
	}
	ri.Cvar_Set("r_volumetricFogHeight", "1");
	R_VolumetricFogPrint();
}

/*
=================
R_VolumetricNoise

Density noise constants (r_volumetricFogNoise, off by default):

  sigma = sigma_plain + m(p) * sigma_noisy
  m(p)  = N_M(lod) * f(n_M; c_M) * N_D(lod) * f(n_D; c_D),  f(n; c) = (1 + c) n^c
  n_M   = noise.r at p / P_M - windOffset_M
  n_D   = noise.g at R30(p / P_D) + offset - windOffset_D

The wind offsets are wrapped to the tile (the texture repeats), in double
precision from the renderer time. A moving medium lowers the history weight
of the noisy media so that the lag of the temporal filter stays below a tenth
of the finest noise feature:

  lag = |wind| * dt * w / (1 - w) <= lambda  ->  w_noise = min(w, lambda / (lambda + |wind| * dt))

False when no medium is noisy (or both contrasts are 0).
=================
*/
#define FROXEL_NOISE_COS30 0.8660254f
#define FROXEL_NOISE_SIN30 0.5f

static qboolean R_VolumetricNoise( VolumetricFogBlock *block, const trRefdef_t *refdef, float historyWeight )
{
	const int mask = r_volumetricFogNoise->integer & 15;
	const float macroContrast = Com_Clamp(0.0f, 4.0f, r_volumetricFogNoiseContrast->value);
	const float detailContrast = Com_Clamp(0.0f, 4.0f, r_volumetricFogNoiseDetailContrast->value);
	if ( !mask || !tr.froxelNoiseImage || (macroContrast <= 0.0f && detailContrast <= 0.0f) )
		return qfalse;

	const float macroPeriod = MAX(64.0f, r_volumetricFogNoiseScale->value);
	const float detailPeriod = MAX(16.0f, r_volumetricFogNoiseDetailScale->value);
	VectorSet4(block->noiseParams, 1.0f / macroPeriod, 1.0f / detailPeriod, macroContrast, detailContrast);

	vec3_t wind = { 0.0f, 0.0f, 0.0f };
	sscanf(r_volumetricFogNoiseWind->string, "%f %f %f", &wind[0], &wind[1], &wind[2]);
	const double seconds = (double)refdef->time * 0.001;
	const double detailWind[3] = {
		FROXEL_NOISE_COS30 * wind[0] - FROXEL_NOISE_SIN30 * wind[1],
		FROXEL_NOISE_SIN30 * wind[0] + FROXEL_NOISE_COS30 * wind[1],
		wind[2] };
	for ( int c = 0; c < 3; c++ )
	{
		const double macro = (double)wind[c] * seconds / macroPeriod;
		const double detail = detailWind[c] * seconds / detailPeriod;
		block->noiseMacroOffset[c] = (float)(macro - floor(macro));
		block->noiseDetailOffset[c] = (float)(detail - floor(detail));
	}
	block->noiseMacroOffset[3] = (mask & 1) ? 1.0f : 0.0f;

	// history weight of the noisy media
	const float speed = VectorLength(wind);
	const float finest = ((detailContrast > 0.0f) ? detailPeriod : macroPeriod) / 16.0f;
	const float lambda = 0.1f * finest;
	const float dt = Com_Clamp(1.0f / 240.0f, 1.0f / 15.0f, refdef->frameTime * 0.001f);
	block->noiseDetailOffset[3] = (speed > 0.0f) ?
		MIN(historyWeight, lambda / (lambda + speed * dt)) : historyWeight;

	// lod = log2(slice thickness / texel size) - 1: one level sharper than the
	// froxel, the jittered positions average the rest over the frames
	const float sliceRatio = powf(s_vf.farZ / s_vf.nearZ, 1.0f / (float)s_vf.depth) - 1.0f;
	VectorSet4(block->noiseLod,
		log2f((float)FROXEL_NOISE_SIZE / macroPeriod) - 1.0f,
		log2f((float)FROXEL_NOISE_SIZE / detailPeriod) - 1.0f,
		sliceRatio,
		1.0f);

	const float *macroNorm = R_VolumetricNoiseNorm(0, macroContrast);
	const float *detailNorm = R_VolumetricNoiseNorm(1, detailContrast);
	for ( int j = 0; j < 16; j++ )
	{
		block->noiseNormMacro[j >> 2][j & 3] = macroNorm[j];
		block->noiseNormDetail[j >> 2][j & 3] = detailNorm[j];
	}

	return qtrue;
}

// the noise settings the history was built with (a change resets it)
static unsigned int R_VolumetricNoiseKey( void )
{
	const float values[5] = {
		(float)r_volumetricFogNoise->integer,
		r_volumetricFogNoiseScale->value,
		r_volumetricFogNoiseContrast->value,
		r_volumetricFogNoiseDetailScale->value,
		r_volumetricFogNoiseDetailContrast->value };
	unsigned int key = 2166136261u;
	const byte *bytes = (const byte *)values;
	for ( size_t i = 0; i < sizeof(values); i++ )
		key = (key ^ bytes[i]) * 16777619u;
	return key;
}

static const viewParms_t *R_VolumetricMainView( void )
{
	for ( int i = tr.numCachedViewParms - 1; i >= 0; i-- )
	{
		if ( tr.cachedViewParms[i].viewParmType == VPT_MAIN )
			return &tr.cachedViewParms[i];
	}
	return NULL;
}

/*
=================
RB_UpdateVolumetricConstants

Decides if this scene builds (or reuses) the froxel volume and appends the
VolumetricFog block. Every scene gets a block, inactive ones with
viewOrigin.w = 0.
=================
*/
void RB_UpdateVolumetricConstants( gpuFrame_t *frame, const trRefdef_t *refdef )
{
	tr.volumetricFogUboOffset = -1;
	if ( !s_vf.resources )
		return;

	VolumetricFogBlock block = {};
	const int frameNumber = backEndData->realFrameNumber;
	const viewParms_t *view = R_VolumetricMainView();

	if ( s_vf.builtFrameNumber != frameNumber )
	{
		// a new frame
		s_vf.frameActive = qfalse;
		s_vf.frameUsable = qfalse;
		s_vf.built = qfalse;
	}
	else
	{
		// another scene of a frame that already owns the volume
		tr.volumetricFogUboOffset = RB_AppendConstantsData(frame, &block, sizeof(block));
		return;
	}

	vec4_t heightFog, heightFogColor, heightFogTop;
	const qboolean heightFogOn = R_VolumetricHeightFog(heightFog, heightFogColor, heightFogTop);
	s_vf.frameHeightFog = qfalse;

	const float nearZ = FROXEL_NEAR;
	float farZ = (r_volumetricFogFar->value > 0.0f) ? r_volumetricFogFar->value : FROXEL_AUTO_FAR;
	farZ = MAX(farZ, nearZ * 4.0f);

	const qboolean worldView = (qboolean)(
		view != NULL &&
		tr.world != NULL &&
		// no fog volume, no height fog, no local fog volume: nothing to do
		(tr.world->numfogs > 1 || heightFogOn || R_FogVolumesInFrustum(view, refdef, farZ)) &&
		tr.renderFbo != NULL &&
		!(refdef->rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE)) &&
		!refdef->doLAGoggles &&
		r_depthPrepass->integer &&
		r_drawfog->integer &&
		view->targetFbo == NULL);

	if ( !worldView )
	{
		tr.volumetricFogUboOffset = RB_AppendConstantsData(frame, &block, sizeof(block));
		return;
	}

	s_vf.builtFrameNumber = frameNumber;
	s_vf.frameScene = frame->currentScene;
	s_vf.frameHeightFog = heightFogOn;

	// projection of the rendered view; the froxel camera drops the SMAA T2x
	// jitter (written to P[2] and P[6], see R_GatherFrameViews)
	matrix_t renderViewProjection, froxelProjection, froxelViewProjection;
	Matrix16Multiply(view->projectionMatrix, view->world.modelViewMatrix, renderViewProjection);
	Matrix16Copy(view->projectionMatrix, froxelProjection);
	froxelProjection[2] = 0.0f;
	froxelProjection[6] = 0.0f;
	Matrix16Multiply(froxelProjection, view->world.modelViewMatrix, froxelViewProjection);

	vec3_t forward, right, up;
	VectorCopy(view->ori.axis[0], forward);
	VectorScale(view->ori.axis[1], -1.0f, right);
	VectorCopy(view->ori.axis[2], up);
	VectorNormalize(forward);
	VectorNormalize(right);
	VectorNormalize(up);

	// the frozen volume keeps its camera (r_volumetricFogFreeze)
	const qboolean freeze = (qboolean)(r_volumetricFogFreeze->integer && s_vf.hasVolume && s_vf.world == tr.world);
	if ( freeze && !s_vf.frozen )
	{
		s_vf.frozen = qtrue;
	}
	else if ( !freeze )
	{
		s_vf.frozen = qfalse;
	}

	// sky distance: the legacy fog cap of a global fog sits at depthForOpaque
	float skyDistance = view->zFar;
	if ( tr.world->globalFog )
		skyDistance = MAX(skyDistance, tr.world->globalFog->parms.depthForOpaque);

	matrix_t invRenderViewProjection;
	if ( !R_VolumetricInvertMatrix(renderViewProjection, invRenderViewProjection) )
		Matrix16Identity(invRenderViewProjection);

	vec4_t viewport;
	VectorSet4(viewport,
		view->viewportX / (float)tr.renderFbo->width,
		view->viewportY / (float)tr.renderFbo->height,
		view->viewportWidth / (float)tr.renderFbo->width,
		view->viewportHeight / (float)tr.renderFbo->height);

	if ( s_vf.frozen )
	{
		block = s_vf.frozenBlock;
		Matrix16Copy(invRenderViewProjection, block.invViewProjection);
		VectorCopy4(viewport, block.viewport);
		block.sliceParams[3] = skyDistance;
		block.debugParams[0] = (float)r_volumetricFogDebug->integer;
		block.debugParams[1] = r_volumetricFogBloom->value;
		block.debugParams[2] = 1.0f;

		s_vf.frameActive = qfalse;
		s_vf.frameUsable = qtrue;
		tr.volumetricFogUboOffset = RB_AppendConstantsData(frame, &block, sizeof(block));
		return;
	}

	// history
	const int debug = r_volumetricFogDebug->integer;
	const unsigned int noiseKey = R_VolumetricNoiseKey();
	const qboolean temporal = (qboolean)(r_volumetricFogTemporal->integer != 0);
	qboolean historyValid = (qboolean)(
		temporal &&
		s_vf.hasVolume &&
		s_vf.volumeFrameNumber + 1 == frameNumber &&
		s_vf.builtVolumeFrame + 1 == frameNumber &&	// the previous volume was really built
		s_vf.builtVolumeImage == s_vf.current &&		// and is the history image of this frame
		s_vf.world == tr.world &&
		s_vf.nearZ == nearZ &&
		s_vf.farZ == farZ &&
		s_vf.debug == debug &&
		s_vf.noiseKey == noiseKey &&
		!r_volumetricFogReset->integer &&
		tr.temporalHistoryValid);
	if ( historyValid )
	{
		if ( Distance(s_vf.origin, view->ori.origin) > FROXEL_CUT_DISTANCE ||
			DotProduct(s_vf.forward, forward) < FROXEL_CUT_COS_ANGLE ||
			fabsf(s_vf.fovX - view->fovX) > FROXEL_CUT_FOV * s_vf.fovX ||
			fabsf(s_vf.fovY - view->fovY) > FROXEL_CUT_FOV * s_vf.fovY )
		{
			historyValid = qfalse;
		}
	}
	if ( r_volumetricFogReset->integer )
		ri.Cvar_Set("r_volumetricFogReset", "0");

	Matrix16Copy(historyValid ? s_vf.viewProjection : froxelViewProjection, block.prevViewProjection);

	s_vf.current ^= 1;
	s_vf.frameActive = qtrue;
	s_vf.frameUsable = qtrue;
	s_vf.hasVolume = qtrue;
	s_vf.volumeFrameNumber = frameNumber;
	s_vf.world = tr.world;
	Matrix16Copy(froxelViewProjection, s_vf.viewProjection);
	VectorCopy(view->ori.origin, s_vf.origin);
	VectorCopy(forward, s_vf.forward);
	s_vf.fovX = view->fovX;
	s_vf.fovY = view->fovY;
	s_vf.nearZ = nearZ;
	s_vf.farZ = farZ;
	s_vf.debug = debug;
	s_vf.noiseKey = noiseKey;
	s_vf.frameIndex++;

	R_VolumetricCullLights(view, refdef, forward);

	// froxel camera
	const float *P = froxelProjection;
	Matrix16Copy(froxelViewProjection, block.viewProjection);
	Matrix16Copy(invRenderViewProjection, block.invViewProjection);
	VectorSet4(block.viewOrigin, view->ori.origin[0], view->ori.origin[1], view->ori.origin[2], 1.0f);
	VectorSet4(block.viewForward, forward[0], forward[1], forward[2], 0.0f);
	for ( int c = 0; c < 3; c++ )
	{
		block.rayForward[c] = forward[c] + right[c] * (P[8] / P[0]) + up[c] * (P[9] / P[5]);
		block.rayRight[c] = right[c] / P[0];
		block.rayUp[c] = up[c] / P[5];
	}
	VectorCopy4(viewport, block.viewport);
	VectorSet4(block.sliceParams, nearZ, farZ, log2f(farZ / nearZ), skyDistance);
	VectorSet4(block.gridSize, (float)s_vf.width, (float)s_vf.height, (float)s_vf.depth, (float)(s_vf.frameIndex & 1023));

	// jitter inside the froxel, a new position every frame (8 frame cycle)
	if ( temporal )
	{
		const unsigned int i = (s_vf.frameIndex & 7) + 1;
		VectorSet4(block.jitter,
			R_VolumetricHalton(i, 2) - 0.5f,
			R_VolumetricHalton(i, 3) - 0.5f,
			R_VolumetricHalton(i, 5) - 0.5f,
			1.0f);
	}

	VectorSet4(block.temporalParams,
		historyValid ? r_volumetricFogHistoryWeight->value : 0.0f,
		historyValid ? 1.0f : 0.0f,
		0.0f,
		4.0f);	// history radiance clamped to [current / 4, current * 4]

	VectorSet4(block.lightParams,
		Com_Clamp(-0.9f, 0.9f, r_volumetricFogAnisotropy->value),
		r_volumetricFogSunScale->value,
		r_volumetricFogDlightScale->value,
		r_volumetricFogStaticScale->value);

	// sun: realtime with the cascaded shadow maps rendered for this view,
	// otherwise the baked sun part of the light grid
	const qboolean splitGrid = (qboolean)(tr.world->volumetricStaticGrid != NULL);
	const qboolean csm = (qboolean)(splitGrid && (view->flags & VPF_USESUNLIGHT) && tr.sunShadowArrayImage != NULL);
	vec3_t sunColor;
	if ( tr.world->volumetricHasSunCells )
		VectorCopy(tr.world->volumetricSunRadiance, sunColor);
	else
		VectorCopy(refdef->sunCol, sunColor);
	VectorSet4(block.sunColor, sunColor[0], sunColor[1], sunColor[2], csm ? 1.0f : 0.0f);
	VectorSet4(block.sunDirection, refdef->sunDir[0], refdef->sunDir[1], refdef->sunDir[2], splitGrid ? 1.0f : 0.0f);

	// light grid, as the legacy fog pass: origin half a cell below, texture
	// coordinates = (p - origin) * inverseSize / bounds
	if ( tr.world->lightGridData )
	{
		vec3_t sampleOrigin;
		VectorMA(tr.world->lightGridOrigin, -0.5f, tr.world->lightGridSize, sampleOrigin);
		VectorSet4(block.gridOrigin, sampleOrigin[0], sampleOrigin[1], sampleOrigin[2], tr.world->lightGridSize[2]);
		for ( int c = 0; c < 3; c++ )
			block.gridScale[c] = tr.world->lightGridInverseSize[c] / (float)MAX(1, tr.world->lightGridBounds[c]);
		block.gridScale[3] = tr.world->lightGridSize[0];	// horizontal cell size
	}

	const qboolean dlightShadows = (qboolean)(
		r_volumetricFogDlightShadows->integer &&
		r_dlightMode->integer >= 2 &&
		tr.pointShadowArrayImage != NULL);
	VectorSet4(block.shadowParams,
		r_shadowCascadeZFar->value,
		(float)r_shadowMapSize->integer,
		dlightShadows ? 1.0f : 0.0f,
		0.0002f);	// cascade depth bias (normalized depth)

	VectorSet4(block.debugParams, (float)debug, r_volumetricFogBloom->value, 0.0f, 0.0f);

	// height fog medium, added to the fog volumes by the injection
	VectorCopy4(heightFog, block.heightFog);
	VectorCopy4(heightFogColor, block.heightFogColor);
	VectorCopy4(heightFogTop, block.heightFogTop);

	// density noise of the selected media
	const qboolean noise = R_VolumetricNoise(&block, refdef, block.temporalParams[0]);
	const int noiseMask = noise ? (r_volumetricFogNoise->integer & 15) : 0;

	// media: every fog volume of the map, as the Fogs block (volumetric units)
	int numFogs = tr.world->numfogs - 1;
	numFogs = Com_Clampi(0, MAX_GPU_FOGS, numFogs);
	block.numFogs = numFogs;
	for ( int i = 0; i < numFogs; i++ )
	{
		const fog_t *fog = tr.world->fogs + i + 1;
		const float extinction = (-logf(1.5f / 255.0f)) / fog->parms.depthForOpaque *
			tr.volumetricFogScale * r_volumetricFogScale->value;
		VectorSet4(block.fogColor[i], fog->color[0], fog->color[1], fog->color[2], extinction);
		VectorCopy4(fog->surface, block.fogPlane[i]);
		VectorSet4(block.fogMins[i], fog->bounds[0][0], fog->bounds[0][1], fog->bounds[0][2], fog->hasSurface ? 1.0f : 0.0f);
		const qboolean noisy = (qboolean)(noiseMask & ((fog == tr.world->globalFog) ? 4 : 2));
		VectorSet4(block.fogMaxs[i], fog->bounds[1][0], fog->bounds[1][1], fog->bounds[1][2], noisy ? 1.0f : 0.0f);
	}

	// local fog volumes: culled, nearest first, per slice lists (tr_fogvolume.cpp).
	// Like the height fog they are outside the BSP fog volumes: transparent
	// surfaces without a fog volume look the volume up too.
	const int numLocalVolumes = R_FogVolumesBuild(&block, view, refdef, forward, nearZ, farZ, s_vf.depth,
		(noiseMask & 8) ? qtrue : qfalse);
	s_vf.frameHeightFog = (qboolean)(heightFogOn || numLocalVolumes > 0);

	s_vf.frozenBlock = block;
	tr.volumetricFogUboOffset = RB_AppendConstantsData(frame, &block, sizeof(block));
}

UniformBlockBinding RB_GetVolumetricFogBlockUniformBinding( void )
{
	const byte currentFrameScene = backEndData->currentFrame->currentScene;
	UniformBlockBinding binding = {};
	binding.ubo = backEndData->currentFrame->ubo[currentFrameScene];
	binding.block = UNIFORM_BLOCK_VOLUMETRIC_FOG;
	binding.offset = (tr.volumetricFogUboOffset == -1) ? 0 : tr.volumetricFogUboOffset;
	return binding;
}

/*
============================================================

Views and draws

============================================================
*/

/*
=================
RB_VolumetricBeginView

Called by RB_BeginDrawingView: does this view use the froxel volume?
=================
*/
void RB_VolumetricBeginView( void )
{
	backEnd.volumetricView = qfalse;
	backEnd.volumetricComposited = qfalse;

	if ( !s_vf.resources || !s_vf.frameUsable )
		return;

	const viewParms_t& viewParms = backEnd.viewParms;
	if ( viewParms.viewParmType != VPT_MAIN || viewParms.isPortal || viewParms.isSkyPortal )
		return;
	if ( viewParms.flags & VPF_DEPTHSHADOW )
		return;
	if ( backEndData->currentFrame->currentScene != s_vf.frameScene )
		return;
	if ( glState.currentFBO != tr.renderFbo || backEnd.framePostProcessed )
		return;
	if ( backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE) )
		return;

	backEnd.volumetricView = qtrue;
}

/*
=================
RB_VolumetricFogMode

How a fogged draw of this sort gets its fog in the current view:
0 = legacy fog, 1 = froxel volume lookup, 2 = none (the composite after the
opaque layers applies it).
=================
*/
int RB_VolumetricFogMode( float sort )
{
	if ( !backEnd.volumetricView || backEnd.depthFill || backEnd.refractionFill )
		return 0;

	// the sort key keeps the integer part of the sort (RB_CreateSortKey)
	return ((int)sort <= SS_FOG) ? 2 : 1;
}

/*
=================
RB_VolumetricHeightFogSurface

The height fog is everywhere, not only inside the fog volumes: surfaces
without a fog volume (fogNum 0) that look the volume up themselves (layers
after SS_FOG) must be drawn with their fog path too. The layers up to SS_FOG
get it from the composite.
=================
*/
qboolean RB_VolumetricHeightFogSurface( float sort )
{
	return (qboolean)(s_vf.frameHeightFog && RB_VolumetricFogMode(sort) == 1);
}

void RB_VolumetricSetupFogDraw( int mode, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers )
{
	if ( !s_vf.resources )
		return;

	uniforms.SetUniformInt(UNIFORM_FROXELFOGMODE, mode);
	if ( mode == 1 )
	{
		samplers.AddStaticImage(tr.froxelIntegratedImage, TB_CUBEMAP);
		samplers.AddStaticImage(tr.froxelTailImage, TB_ENVBRDFMAP);
	}
}

/*
============================================================

GPU passes

============================================================
*/

// GPU timers (r_speeds 100), same bookkeeping as RB_BeginTimedBlock
static int RB_VolumetricBeginTimer( const char *name )
{
	if ( !glRefConfig.timerQuery || r_speeds->integer != 100 )
		return -1;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	if ( tr.numTimedBlocks >= (MAX_GPU_TIMERS / 2) || frame->numTimers + 2 > MAX_GPU_TIMERS )
		return -1;

	const int handle = tr.numTimedBlocks++;
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	gpuTimedBlock_t *timedBlock = frame->timedBlocks + handle;
	timedBlock->beginTimer = timer->queryName;
	timedBlock->name = name;
	frame->numTimedBlocks++;

	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
	return handle;
}

static void RB_VolumetricEndTimer( int handle )
{
	if ( handle < 0 )
		return;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	frame->timedBlocks[handle].endTimer = timer->queryName;
	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
}

static void RB_VolumetricViewViewport( void )
{
	GL_SetViewportAndScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
}

static void RB_VolumetricBindBlocks( void )
{
	const byte scene = backEndData->currentFrame->currentScene;
	const GLuint frameUbo = backEndData->currentFrame->ubo[scene];

	if ( tr.sceneUboOffset == -1 )
		RB_BindUniformBlock(tr.staticUbo, UNIFORM_BLOCK_SCENE, tr.defaultSceneUboOffset);
	else
		RB_BindUniformBlock(frameUbo, UNIFORM_BLOCK_SCENE, tr.sceneUboOffset);

	if ( tr.lightsUboOffset == -1 )
		RB_BindUniformBlock(tr.staticUbo, UNIFORM_BLOCK_LIGHTS, tr.defaultLightsUboOffset);
	else
		RB_BindUniformBlock(frameUbo, UNIFORM_BLOCK_LIGHTS, tr.lightsUboOffset);

	const UniformBlockBinding binding = RB_GetVolumetricFogBlockUniformBinding();
	RB_BindUniformBlock(binding.ubo, binding.block, binding.offset);
}

/*
=================
RB_VolumetricBuild

Injection and integration of the froxel volume. Called after the depth
prepass of the main view: the shadow maps of this frame are rendered.
=================
*/
void RB_VolumetricBuild( void )
{
	if ( !backEnd.volumetricView || !s_vf.frameActive || s_vf.built )
		return;

	s_vf.built = qtrue;

	FBO_t *oldFbo = glState.currentFBO;
	const int current = s_vf.current;
	const int previous = current ^ 1;
	s_vf.builtVolumeFrame = s_vf.volumeFrameNumber;
	s_vf.builtVolumeImage = current;

	R_PushDebugGroup(AL_STAGE, "Froxel fog");
	GL_Cull(CT_TWO_SIDED);
	GL_State(GLS_DEPTHTEST_DISABLE);
	RB_VolumetricBindBlocks();

	// injection + temporal filter, one slice per draw
	int timer = RB_VolumetricBeginTimer("Froxel fog inject");
	{
		shaderProgram_t *sp = &tr.volumetricInjectShader;
		FBO_Bind(tr.froxelInjectFbo);
		GL_SetViewportAndScissor(0, 0, s_vf.width, s_vf.height);
		GLSL_BindProgram(sp);

		image_t *staticGrid = tr.world->volumetricStaticGrid ? tr.world->volumetricStaticGrid : tr.whiteImage3D;
		image_t *sunGrid = tr.world->volumetricSunGrid ? tr.world->volumetricSunGrid : tr.whiteImage3D;
		if ( !tr.world->volumetricStaticGrid && tr.world->volumetricLightMaps[0] )
			staticGrid = tr.world->volumetricLightMaps[0];

		GL_BindToTMU(tr.froxelInjectImage[previous], TB_COLORMAP);
		GL_BindToTMU(tr.froxelNoiseImage, TB_DELUXEMAP);
		GL_BindToTMU(staticGrid, TB_LIGHTMAP);
		GL_BindToTMU(sunGrid, TB_NORMALMAP);
		if ( tr.sunShadowArrayImage )
			GL_BindToTMU(tr.sunShadowArrayImage, TB_SHADOWMAP);
		if ( tr.pointShadowArrayImage )
			GL_BindToTMU(tr.pointShadowArrayImage, TB_SHADOWMAPARRAY);

		for ( int k = 0; k < s_vf.depth; k++ )
		{
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				tr.froxelInjectImage[current]->texnum, 0, k);
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
				tr.froxelDynamicImage->texnum, 0, k);
			GLSL_SetUniformInt(sp, UNIFORM_FROXELSLICE, k);
			GLSL_SetUniformInt(sp, UNIFORM_LIGHTMASK, s_vf.lightMask[k]);
			RB_InstantTriangle();
		}
	}
	RB_VolumetricEndTimer(timer);

	// front to back integration, one slice per draw
	timer = RB_VolumetricBeginTimer("Froxel fog integrate");
	{
		shaderProgram_t *sp = &tr.volumetricIntegrateShader;
		FBO_Bind(tr.froxelIntegrateFbo);
		GL_SetViewportAndScissor(0, 0, s_vf.width, s_vf.height);
		GLSL_BindProgram(sp);
		GL_BindToTMU(tr.froxelInjectImage[current], TB_COLORMAP);
		GL_BindToTMU(tr.froxelDynamicImage, TB_NORMALMAP);

		// the tail is draw buffer 2, masked by default with SSR / SSGI (the
		// screen-space attachments of renderFbo, see GL_SetScreenAuxWrite)
		GL_SetScreenAuxWrite(true);

		const GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
		for ( int k = 0; k < s_vf.depth; k++ )
		{
			const int carryWrite = k & 1;
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				tr.froxelIntegratedImage->texnum, 0, k);
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
				GL_TEXTURE_2D, tr.froxelCarryImage[carryWrite]->texnum, 0);
			qglDrawBuffers((k == s_vf.depth - 1) ? 3 : 2, bufs);

			GL_BindToTMU(tr.froxelCarryImage[carryWrite ^ 1], TB_LIGHTMAP);
			GLSL_SetUniformInt(sp, UNIFORM_FROXELSLICE, k);
			RB_InstantTriangle();
		}

		GL_SetScreenAuxWrite(false);
	}
	RB_VolumetricEndTimer(timer);

	FBO_Bind(oldFbo);
}

qboolean RB_VolumetricCompositeActive( void )
{
	return (qboolean)(
		backEnd.volumetricView &&
		!backEnd.volumetricComposited &&
		!backEnd.depthFill &&
		!backEnd.refractionFill &&
		(s_vf.built || s_vf.frozen));
}

/*
=================
RB_VolumetricComposite

Fog of everything drawn so far (sort <= SS_FOG, the sky included) from the
depth buffer: color * T + S, glow * T. Called by RB_SubmitRenderPass with
renderFbo bound.
=================
*/
void RB_VolumetricComposite( void )
{
	if ( !RB_VolumetricCompositeActive() )
		return;

	backEnd.volumetricComposited = qtrue;

	FBO_t *oldFbo = glState.currentFBO;
	const int timer = RB_VolumetricBeginTimer("Froxel fog composite");

	// MSAA: the depth texture is the resolve target
	if ( tr.msaaResolveFbo )
	{
		// blits are clipped by the scissor rectangle
		GL_SetViewportAndScissor(0, 0, tr.renderFbo->width, tr.renderFbo->height);
		FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

	shaderProgram_t *sp = &tr.volumetricCompositeShader;
	FBO_Bind(tr.froxelCompositeFbo);
	RB_VolumetricViewViewport();
	GL_Cull(CT_TWO_SIDED);
	// color * T + S (source alpha = T), glow * T. The destination alpha is
	// kept: GL_State only masks all channels, so mask alpha directly and
	// restore the full mask GL_State assumes afterwards.
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_SRC_ALPHA);
	qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	GLSL_BindProgram(sp);
	RB_VolumetricBindBlocks();
	GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
	GL_BindToTMU(tr.froxelIntegratedImage, TB_CUBEMAP);
	GL_BindToTMU(tr.froxelTailImage, TB_ENVBRDFMAP);
	RB_InstantTriangle();
	qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GL_ResetScreenAuxWrite();

	RB_VolumetricEndTimer(timer);

	FBO_Bind(oldFbo);
	RB_VolumetricViewViewport();
}

/*
=================
RB_VolumetricDebugOverlay

r_volumetricFogDebug views, drawn over the tone mapped frame
=================
*/
void RB_VolumetricDebugOverlay( void )
{
	if ( !s_vf.resources || !r_volumetricFogDebug->integer || !s_vf.frameUsable )
		return;
	if ( backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE) )
		return;

	shaderProgram_t *sp = &tr.volumetricDebugShader;
	FBO_Bind(NULL);
	GL_SetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	GL_Cull(CT_TWO_SIDED);
	// view 18 (local fog volume bounds) is drawn over the frame
	if ( r_volumetricFogDebug->integer == 18 )
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	else
		GL_State(GLS_DEPTHTEST_DISABLE);
	GLSL_BindProgram(sp);
	RB_VolumetricBindBlocks();

	// MSAA: renderDepthImage holds the resolved depth of the main view
	GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
	GL_BindToTMU(tr.froxelInjectImage[s_vf.current], TB_LIGHTMAP);
	GL_BindToTMU(tr.froxelDynamicImage, TB_NORMALMAP);
	GL_BindToTMU(tr.froxelIntegratedImage, TB_CUBEMAP);
	GL_BindToTMU(tr.froxelTailImage, TB_ENVBRDFMAP);
	GL_BindToTMU(tr.froxelNoiseImage, TB_DELUXEMAP);
	RB_InstantTriangle();
}
