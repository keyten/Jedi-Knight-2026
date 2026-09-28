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
// GL 4.3 (r_gl43) uses compute injection and column integration. GL 3.2
// keeps layered raster injection and one integration draw per slice.
//
// Only the main view of the first world scene of a frame uses the volume;
// portals, mirrors, sky portals, the LA goggles and other scenes use the
// legacy fog path.

#include "tr_local.h"

#include <algorithm>
#include <vector>

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
	qboolean computeAvailable;	// limits of the current resource dimensions
	qboolean rgb;				// RGB extinction (r_volumetricFogRGBExtinction, latched)
	int width, height, depth;

	// the volume of this frame
	qboolean frameActive;		// injected and integrated this frame
	qboolean frameUsable;		// lookups allowed this frame (built or frozen)
	int frameScene;				// scene of the frame that owns the volume
	int builtFrameNumber;
	qboolean built;				// GPU passes of this frame ran
	int current;				// froxelInjectImage written this frame
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
	unsigned int mediumKey;
	unsigned int frameIndex;

	// frozen froxel camera (r_volumetricFogFreeze)
	qboolean frozen;
	VolumetricFogBlock frozenBlock;
};

static froxelState_t s_vf;

qboolean R_VolumetricComputeAvailable( void )
{
	if ( !s_vf.resources || !R_HasModernFeatures(MODERN_COMPUTE | MODERN_IMAGE_LOAD_STORE) )
		return qfalse;
	if ( glRefConfig.maxImageUnits < 6 || glRefConfig.maxComputeImageUniforms < 6 ||
		glRefConfig.maxComputeWorkGroupInvocations < 64 ||
		glRefConfig.maxComputeWorkGroupSize[0] < 8 ||
		glRefConfig.maxComputeWorkGroupSize[1] < 8 ||
		glRefConfig.maxComputeWorkGroupSize[2] < 4 ||
		(s_vf.width + 3) / 4 > glRefConfig.maxComputeWorkGroupCount[0] ||
		(s_vf.height + 3) / 4 > glRefConfig.maxComputeWorkGroupCount[1] ||
		(s_vf.depth + 3) / 4 > glRefConfig.maxComputeWorkGroupCount[2] )
		return qfalse;

	GLint samplers = 0, combinedSamplers = 0, blocks = 0;
	qglGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &samplers);
	qglGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combinedSamplers);
	qglGetIntegerv(GL_MAX_COMPUTE_UNIFORM_BLOCKS, &blocks);
	// Stage limits count active samplers, not the largest global TMU index.
	return (qboolean)(samplers >= (s_vf.rgb ? 14 : 13) &&
		combinedSamplers > (s_vf.rgb ? TB_FROXELEXTINCTION : TB_LIGHTCOOKIES) && blocks >= 3);
}

// dynamic light lists of the froxels (R_VolumetricBuildLightLists): GL objects
// created with the froxel resources, deleted by R_ShutdownVolumetric
static struct
{
	GLuint lightBuffers[MAX_FRAMES];
	GLuint listBuffers[MAX_FRAMES];
	image_t lightImages[MAX_FRAMES];	// buffer textures, bound like images
	image_t listImages[MAX_FRAMES];
	int lightSlot;						// buffers of this frame
	qboolean hasLights;					// this frame's volume has light lists
	std::vector<uint32_t> lightCounts;
	std::vector<uint32_t> lightCursor;
	std::vector<uint32_t> lightList;
} s_vfl;

qboolean R_VolumetricFroxelEnabled( void )
{
	return s_vf.resources;
}

qboolean R_VolumetricFroxelRGB( void )
{
	return (qboolean)(s_vf.resources && s_vf.rgb);
}

/*
=================
R_VolumetricExtinctionColor

Relative extinction per channel of a medium (r_volumetricFogRGBExtinction):
sigma_t.rgb = sigma * c, c normalized to mean 1, so the scalar sigma (the
alpha channels, self-shadow, multiple scattering, history weights) is the
mean of sigma_t.rgb and a medium keeps its average opacity. Negative values
are clamped, black (or no color) is neutral (1 1 1).
=================
*/
void R_VolumetricExtinctionColor( const float *in, vec3_t out )
{
	vec3_t c;
	for ( int i = 0; i < 3; i++ )
		c[i] = (in && in[i] > 0.0f) ? in[i] : 0.0f;
	const float mean = (c[0] + c[1] + c[2]) * (1.0f / 3.0f);
	if ( !(mean > 0.0f) )
	{
		VectorSet(out, 1.0f, 1.0f, 1.0f);
		return;
	}
	VectorScale(c, 1.0f / mean, out);
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

// Allocate carry only for raster, including an optional compute compile/link
// failure after FBO initialization. Slice zero initializes the running state.
void R_VolumetricEnsureRasterCarry( void )
{
	if ( !s_vf.resources || tr.froxelCarryImage[0] )
		return;
	for ( int i = 0; i < 2; ++i )
	{
		tr.froxelCarryImage[i] = R_CreateImage(
			va("*froxelCarry%d", i), NULL, s_vf.width, s_vf.height, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
		if ( s_vf.rgb )
			tr.froxelCarryTImage[i] = R_CreateImage(
				va("*froxelCarryT%d", i), NULL, s_vf.width, s_vf.height, IMGTYPE_COLORALPHA,
				IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
	}
	if ( tr.froxelIntegrateFbo )
	{
		FBO_t *oldFbo = glState.currentFBO;
		FBO_Bind(tr.froxelIntegrateFbo);
		FBO_AttachTextureImage(tr.froxelCarryImage[0], 1);
		if ( s_vf.rgb )
			FBO_AttachTextureImage(tr.froxelCarryTImage[0], 3);
		const GLenum bufs[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
		qglDrawBuffers(s_vf.rgb ? 4 : 2, bufs);
		R_CheckFBO(tr.froxelIntegrateFbo);
		FBO_Bind(oldFbo);
	}
}

void R_CreateVolumetricImages( int width, int height )
{
	Com_Memset(&s_vf, 0, sizeof(s_vf));
	s_vf.builtVolumeFrame = -1;
	s_vf.builtVolumeImage = -1;
	tr.froxelInjectImage[0] = tr.froxelInjectImage[1] = NULL;
	tr.froxelDynamicImage = NULL;
	tr.froxelParticleLightImage = NULL;
	tr.froxelIntegratedImage = NULL;
	tr.froxelCarryImage[0] = tr.froxelCarryImage[1] = NULL;
	tr.froxelTailImage = NULL;
	tr.froxelNoiseImage = NULL;
	tr.froxelMediaImage = NULL;
	tr.froxelExtinctionImage[0] = tr.froxelExtinctionImage[1] = NULL;
	tr.froxelTransmittanceImage = NULL;
	tr.froxelCarryTImage[0] = tr.froxelCarryTImage[1] = NULL;

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
	}

	tr.froxelDynamicImage = R_CreateImage3D(
		"*froxelDynamic", NULL, s_vf.width, s_vf.height, s_vf.depth, GL_R11F_G11F_B10F);
	// sprite particle light field (r_particleLighting, latched): the incident
	// light of every froxel, a third layered attachment of the injection
	if ( r_particleLighting->integer )
	{
		tr.froxelParticleLightImage = R_CreateImage3D(
			"*froxelParticleLight", NULL, s_vf.width, s_vf.height, s_vf.depth, GL_R11F_G11F_B10F);
	}
	tr.froxelIntegratedImage = R_CreateImage3D(
		"*froxelIntegrated", NULL, s_vf.width, s_vf.height, s_vf.depth, GL_RGBA16F);
	tr.froxelTailImage = R_CreateImage(
		"*froxelTail", NULL, s_vf.width, s_vf.height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
	// media self-shadow (r_volumetricSelfShadow, latched): the extinction of
	// this frame, built before the injection, read along the light rays
	if ( r_volumetricSelfShadow->integer )
	{
		tr.froxelMediaImage = R_CreateImage3D(
			"*froxelMedia", NULL, s_vf.width, s_vf.height, s_vf.depth, GL_R16F);
	}
	// RGB extinction (r_volumetricFogRGBExtinction, latched): sigma_t.rgb next to the
	// injected volume (history ping-pong) and T.rgb next to the integrated one,
	// RGBA16F (R11G11B10F bands T near 1 and drifts in the temporal filter).
	// 24 bytes per froxel, nothing in the scalar mode.
	if ( r_volumetricFogRGBExtinction->integer )
	{
		GLint units = 0;
		qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
		if ( units > TB_FROXELCARRYT )
		{
			s_vf.rgb = qtrue;
			for ( int i = 0; i < 2; i++ )
			{
				tr.froxelExtinctionImage[i] = R_CreateImage3D(
					va("*froxelExtinction%d", i), NULL, s_vf.width, s_vf.height, s_vf.depth, GL_RGBA16F);
			}
			tr.froxelTransmittanceImage = R_CreateImage3D(
				"*froxelTransmittance", NULL, s_vf.width, s_vf.height, s_vf.depth, GL_RGBA16F);
		}
		else
		{
			ri.Printf(PRINT_WARNING, "r_volumetricFogRGBExtinction: needs more than %d texture units, scalar extinction is used\n",
				TB_FROXELCARRYT);
		}
	}

	R_CreateVolumetricNoiseImage();

	s_vf.resources = qtrue;
	s_vf.computeAvailable = R_VolumetricComputeAvailable();
	// A map reload can retain GPU programs and skip the shader loader. If
	// the previous compute compilation failed, recreate raster carry now.
	if ( !s_vf.computeAvailable || (tr.volumetricInjectShader.program &&
		(!tr.volumetricInjectComputeShader.program || !tr.volumetricIntegrateComputeShader.program)) )
		R_VolumetricEnsureRasterCarry();

	if ( !r_depthPrepass->integer )
		ri.Printf(PRINT_WARNING, "r_volumetricFog 2 needs r_depthPrepass 1, the legacy volumetric fog is used\n");

	ri.Printf(PRINT_ALL, "Froxel volumetric fog: %d x %d x %d froxels (%d pixels per froxel)%s\n",
		s_vf.width, s_vf.height, s_vf.depth, gridScale, s_vf.rgb ? ", RGB extinction" : "");
}

// draw buffers of the injection: 0 media, 1 dynamic light, 2 particle light
// (r_particleLighting), 3 sigma_t.rgb (r_volumetricFogRGBExtinction); absent ones GL_NONE
static int R_VolumetricInjectDrawBuffers( GLenum bufs[4] )
{
	bufs[0] = GL_COLOR_ATTACHMENT0;
	bufs[1] = GL_COLOR_ATTACHMENT1;
	bufs[2] = tr.froxelParticleLightImage ? GL_COLOR_ATTACHMENT2 : GL_NONE;
	bufs[3] = GL_COLOR_ATTACHMENT3;
	if ( s_vf.rgb )
		return 4;
	return tr.froxelParticleLightImage ? 3 : 2;
}

void R_CreateVolumetricFBOs( void )
{
	tr.froxelMediaFbo = NULL;
	tr.froxelInjectFbo = NULL;
	tr.froxelIntegrateFbo = NULL;
	tr.froxelCompositeFbo = NULL;

	if ( !s_vf.resources )
		return;

	// injection: froxelInjectImage[current] and the dynamic light volume,
	// layered (every slice in one instanced draw, the geometry shader selects
	// the layer)
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
	if ( tr.froxelParticleLightImage )
	{
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
			tr.froxelParticleLightImage->texnum, 0, 0);
		glState.currentFBO->colorImage[2] = tr.froxelParticleLightImage;
		glState.currentFBO->colorBuffers[2] = tr.froxelParticleLightImage->texnum;
	}
	if ( s_vf.rgb )
	{
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
			tr.froxelExtinctionImage[0]->texnum, 0, 0);
		glState.currentFBO->colorImage[3] = tr.froxelExtinctionImage[0];
		glState.currentFBO->colorBuffers[3] = tr.froxelExtinctionImage[0]->texnum;
	}
	{
		GLenum bufs[4];
		const int numBufs = R_VolumetricInjectDrawBuffers(bufs);
		qglDrawBuffers(numBufs, bufs);
	}
	R_CheckFBO(tr.froxelInjectFbo);

	// media (r_volumetricSelfShadow): the extinction volume alone, layered
	if ( tr.froxelMediaImage )
	{
		tr.froxelMediaFbo = FBO_Create("_froxelMedia", s_vf.width, s_vf.height);
		FBO_Bind(tr.froxelMediaFbo);
		qglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tr.froxelMediaImage->texnum, 0);
		glState.currentFBO->colorImage[0] = tr.froxelMediaImage;
		glState.currentFBO->colorBuffers[0] = tr.froxelMediaImage->texnum;
		const GLenum buf = GL_COLOR_ATTACHMENT0;
		qglDrawBuffers(1, &buf);
		R_CheckFBO(tr.froxelMediaFbo);
	}

	// integration: a layer of the integrated volume and the carried state
	tr.froxelIntegrateFbo = FBO_Create("_froxelIntegrate", s_vf.width, s_vf.height);
	FBO_Bind(tr.froxelIntegrateFbo);
	qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		tr.froxelIntegratedImage->texnum, 0, 0);
	glState.currentFBO->colorImage[0] = tr.froxelIntegratedImage;
	glState.currentFBO->colorBuffers[0] = tr.froxelIntegratedImage->texnum;
	if ( tr.froxelCarryImage[0] )
		FBO_AttachTextureImage(tr.froxelCarryImage[0], 1);
	if ( s_vf.rgb )
	{
		// RGB extinction: a layer of T.rgb and its carry
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
			tr.froxelTransmittanceImage->texnum, 0, 0);
		glState.currentFBO->colorImage[2] = tr.froxelTransmittanceImage;
		glState.currentFBO->colorBuffers[2] = tr.froxelTransmittanceImage->texnum;
		if ( tr.froxelCarryTImage[0] )
			FBO_AttachTextureImage(tr.froxelCarryTImage[0], 3);
	}
	{
		const GLenum bufs[4] = { GL_COLOR_ATTACHMENT0,
			tr.froxelCarryImage[0] ? GL_COLOR_ATTACHMENT1 : GL_NONE, GL_COLOR_ATTACHMENT2,
			tr.froxelCarryTImage[0] ? GL_COLOR_ATTACHMENT3 : GL_NONE };
		qglDrawBuffers(s_vf.rgb ? 4 : 2, bufs);
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
			if ( tr.froxelParticleLightImage )
			{
				qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
					tr.froxelParticleLightImage->texnum, 0, k);
				qglClearBufferfv(GL_COLOR, 2, zero);
			}
			if ( s_vf.rgb )
			{
				// draw buffer 3 (R_VolumetricInjectDrawBuffers)
				for ( int i = 0; i < 2; i++ )
				{
					qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
						tr.froxelExtinctionImage[i]->texnum, 0, k);
					qglClearBufferfv(GL_COLOR, 3, zero);
				}
			}
		}
		if ( s_vf.rgb )
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, 0, 0);
		if ( tr.froxelParticleLightImage )
		{
			// the tail below is a 2D image: no layered attachment next to it
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, 0, 0);
		}
		// tail light: none until the first build
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, tr.froxelTailImage->texnum, 0);
		qglClearBufferfv(GL_COLOR, 0, zero);
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			tr.froxelInjectImage[0]->texnum, 0, 0);
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
			tr.froxelDynamicImage->texnum, 0, 0);
		if ( tr.froxelParticleLightImage )
		{
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
				tr.froxelParticleLightImage->texnum, 0, 0);
		}
		if ( s_vf.rgb )
		{
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
				tr.froxelExtinctionImage[0]->texnum, 0, 0);
		}

		// media: empty (the layered attachment clears every layer)
		if ( tr.froxelMediaFbo )
		{
			FBO_Bind(tr.froxelMediaFbo);
			qglClearBufferfv(GL_COLOR, 0, zero);
		}

		FBO_Bind(tr.froxelIntegrateFbo);
		const float noFogRGB[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		for ( int k = 0; k < s_vf.depth; k++ )
		{
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				tr.froxelIntegratedImage->texnum, 0, k);
			qglClearBufferfv(GL_COLOR, 0, noFog);
			if ( s_vf.rgb )
			{
				qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
					tr.froxelTransmittanceImage->texnum, 0, k);
				qglClearBufferfv(GL_COLOR, 2, noFogRGB);
			}
		}
		if ( s_vf.rgb )
		{
			qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
				tr.froxelTransmittanceImage->texnum, 0, 0);
			for ( int i = 0; tr.froxelCarryTImage[0] && i < 2; i++ )
			{
				qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
					GL_TEXTURE_2D, tr.froxelCarryTImage[i]->texnum, 0);
				qglClearBufferfv(GL_COLOR, 3, noFogRGB);
			}
			if ( tr.froxelCarryTImage[0] )
				qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
					GL_TEXTURE_2D, tr.froxelCarryTImage[0]->texnum, 0);
		}
		qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			tr.froxelIntegratedImage->texnum, 0, 0);
		for ( int i = 0; tr.froxelCarryImage[0] && i < 2; i++ )
		{
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
				GL_TEXTURE_2D, tr.froxelCarryImage[i]->texnum, 0);
			qglClearBufferfv(GL_COLOR, 1, noFog);
		}
		if ( tr.froxelCarryImage[0] )
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
				GL_TEXTURE_2D, tr.froxelCarryImage[0]->texnum, 0);

		GL_ResetScreenAuxWrite();
	}

	// dynamic light lists: buffer textures per frame (R_VolumetricBuildLightLists)
	if ( !s_vfl.lightBuffers[0] && qglTexBuffer )
	{
		qglGenBuffers(MAX_FRAMES, s_vfl.lightBuffers);
		qglGenBuffers(MAX_FRAMES, s_vfl.listBuffers);
		for ( int f = 0; f < MAX_FRAMES; f++ )
		{
			image_t *images[2] = { &s_vfl.lightImages[f], &s_vfl.listImages[f] };
			const GLuint buffers[2] = { s_vfl.lightBuffers[f], s_vfl.listBuffers[f] };
			const GLenum formats[2] = { GL_RGBA32F, GL_R32UI };
			for ( int b = 0; b < 2; b++ )
			{
				Com_Memset(images[b], 0, sizeof(image_t));
				Q_strncpyz(images[b]->imgName, va("*froxelLights%d_%d", f, b), sizeof(images[b]->imgName));
				images[b]->flags = IMGFLAG_TEXBUFFER;
				qglBindBuffer(GL_TEXTURE_BUFFER, buffers[b]);
				qglBufferData(GL_TEXTURE_BUFFER, 16, NULL, GL_STREAM_DRAW);
				qglGenTextures(1, &images[b]->texnum);
				GL_BindToTMU(images[b], TB_FPLUS_LIGHTS);
				qglTexBuffer(GL_TEXTURE_BUFFER, formats[b], buffers[b]);
			}
		}
		qglBindBuffer(GL_TEXTURE_BUFFER, 0);
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

void R_ShutdownVolumetric( void )
{
	if ( s_vfl.lightBuffers[0] )
	{
		for ( int f = 0; f < MAX_FRAMES; f++ )
		{
			image_t *images[2] = { &s_vfl.lightImages[f], &s_vfl.listImages[f] };
			for ( int b = 0; b < 2; b++ )
			{
				for ( int u = 0; u < MAX_TEXTURE_UNITS; u++ )
				{
					if ( glState.currenttextures[u] == (int)images[b]->texnum )
						glState.currenttextures[u] = 0;
				}
				qglDeleteTextures(1, &images[b]->texnum);
			}
		}
		qglDeleteBuffers(MAX_FRAMES, s_vfl.lightBuffers);
		qglDeleteBuffers(MAX_FRAMES, s_vfl.listBuffers);
	}
	Com_Memset(s_vfl.lightBuffers, 0, sizeof(s_vfl.lightBuffers));
	Com_Memset(s_vfl.listBuffers, 0, sizeof(s_vfl.listBuffers));
	s_vfl.hasLights = qfalse;
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

// GPU interpretation of a half float (denormals included)
static float R_VolumetricHalfToFloat( uint16_t h )
{
	const int exponent = (h >> 10) & 0x1f;
	const int fraction = h & 0x3ff;
	float value;
	if ( exponent == 0 )
		value = ldexpf((float)fraction, -24);
	else if ( exponent == 0x1f )
		value = fraction ? 0.0f : 65504.0f;
	else
		value = ldexpf((float)(fraction | 0x400), exponent - 25);
	return (h & 0x8000) ? -value : value;
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

The non-sun remainder is split once more, so that the froxel fog can give
the directed light grid part a phase function along its baked direction
(r_volumetricFogStaticDirectional), with isotropic + directed + sun == legacy
(f = sun fraction of the cell, exact in float, only half rounding remains):

  HDR (legacy = ambient + direct):
    directed  D = (1 - f) * direct
    isotropic I = ambient
  LDR (legacy = max(ambient, direct) = ambient + max(0, direct - ambient)):
    directed  D = min((1 - f) * max(0, direct - ambient), legacy - sun)
    isotropic I = legacy - sun - D
    where the legacy max picked the ambient light nothing is directed.

The direction is stored weighted by the luminance of D, so that trilinear
filtering between cells lit from different directions shortens it: the
injection falls back to isotropic in proportion (coherence). Cells in walls
(styles[0] == LS_LSNONE) keep their light in I, their direction is undefined.

The split only knows the light direction: a lamp straight above can look like
a high sun. The alpha of the static texture is the sun trust of the cell: 1
when a ray towards the sun from the cell center or one of four corners (a
tetrahedron, half a cell out) reaches the sky, 0 when the world blocks them all
(indoors the sun part stays baked light). World geometry is static, so this is
traced once here instead of probing the cascades per froxel every frame.
=================
*/
#define FROXEL_SUN_TRACE_DISTANCE 65536.0f

// true when the world does not block the way from start towards the sun
static qboolean R_VolumetricSunVisible( const vec3_t start, const vec3_t sunDir )
{
	vec3_t end;
	VectorMA(start, FROXEL_SUN_TRACE_DISTANCE, sunDir, end);

	trace_t trace;
	Com_Memset(&trace, 0, sizeof(trace));
#ifdef REND2_SP
	ri.SV_Trace(&trace, start, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
	if ( trace.startsolid || trace.allsolid )
		return qfalse;
	// the sky brushes are solid: reaching one is reaching the sky
	return (qboolean)(trace.fraction >= 1.0f || (trace.surfaceFlags & SURF_SKY));
}

static float R_VolumetricSunTrust( const world_t *world, int cell, const vec3_t sunDir )
{
	const int bx = world->lightGridBounds[0];
	const int by = world->lightGridBounds[1];
	const int gridPos[3] = { cell % bx, (cell / bx) % by, cell / (bx * by) };

	vec3_t center;
	for ( int c = 0; c < 3; c++ )
		center[c] = world->lightGridOrigin[c] + gridPos[c] * world->lightGridSize[c];

	static const float corners[5][3] = {
		{ 0.0f, 0.0f, 0.0f },
		{ 1.0f, 1.0f, 1.0f }, { 1.0f, -1.0f, -1.0f }, { -1.0f, 1.0f, -1.0f }, { -1.0f, -1.0f, 1.0f } };
	for ( int k = 0; k < 5; k++ )
	{
		vec3_t start;
		for ( int c = 0; c < 3; c++ )
			start[c] = center[c] + 0.5f * corners[k][c] * world->lightGridSize[c];
		if ( R_VolumetricSunVisible(start, sunDir) )
			return 1.0f;
	}
	return 0.0f;
}

void R_BuildVolumetricLightGrid( world_t *world )
{
	world->volumetricStaticGrid = NULL;
	world->volumetricSunGrid = NULL;
	world->volumetricDirGrid = NULL;
	world->volumetricDirVecGrid = NULL;
	world->volumetricHasSunCells = qfalse;
	VectorClear(world->volumetricSunRadiance);
	world->particleLightReference = 0.0f;

	if ( r_volumetricFog->integer != 2 || !world->lightGridData || world->numGridArrayElements <= 0 )
		return;

	const int numCells = world->numGridArrayElements;
	if ( numCells != world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] )
	{
		ri.Printf(PRINT_WARNING, "R_BuildVolumetricLightGrid: light grid size mismatch, no sun split\n");
	}

	const qboolean splitSun = tr.sunParsed;
	const qboolean traceTrust = (qboolean)(splitSun &&
		numCells == world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2]);
	const int traceStart = ri.Milliseconds();
	int numTraced = 0, numTrusted = 0;
	vec3_t sunDir;
	VectorCopy(tr.sunDirection, sunDir);
	VectorNormalize(sunDir);

	uint16_t *staticData = (uint16_t *)Z_Malloc(numCells * sizeof(uint16_t) * 4, TAG_TEMP_WORKSPACE, qtrue);
	uint16_t *sunData = (uint16_t *)Z_Malloc(numCells * sizeof(uint16_t) * 4, TAG_TEMP_WORKSPACE, qtrue);
	uint16_t *dirData = (uint16_t *)Z_Malloc(numCells * sizeof(uint16_t) * 4, TAG_TEMP_WORKSPACE, qtrue);
	uint16_t *dirVecData = (uint16_t *)Z_Malloc(numCells * sizeof(uint16_t) * 4, TAG_TEMP_WORKSPACE, qtrue);
	int numDirCells = 0;
	float maxError = 0.0f, sumError = 0.0f, maxRelError = 0.0f;
	float *sunLuma = (float *)Z_Malloc(numCells * sizeof(float), TAG_TEMP_WORKSPACE, qtrue);
	int numSunCells = 0;
	vec3_t sunColorSum = { 0.0f, 0.0f, 0.0f };
	double referenceSum = 0.0;
	int numReferenceCells = 0;

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

		// direction towards the light, as R_SetupEntityLightingGrid (256 steps per turn)
		const float lat = data->latLong[1] * (2.0f * M_PI / 256.0f);
		const float lng = data->latLong[0] * (2.0f * M_PI / 256.0f);
		vec3_t cellDir;
		cellDir[0] = cosf(lat) * sinf(lng);
		cellDir[1] = sinf(lat) * sinf(lng);
		cellDir[2] = cosf(lng);

		float sunFraction = 0.0f;
		if ( splitSun )
		{
			sunFraction = R_VolumetricSmoothstep(
				FROXEL_SUN_COS_OUTER, FROXEL_SUN_COS_INNER, DotProduct(cellDir, sunDir));
		}

		const qboolean validCell = (qboolean)(data->styles[0] != LS_LSNONE);
		vec3_t sun, directed, isotropic;
		for ( int c = 0; c < 3; c++ )
		{
			sun[c] = MIN(sunFraction * direct[c], total[c]);
			const float rest = total[c] - sun[c];
			float d = 0.0f;
			if ( validCell )
			{
				d = world->hdrLightGrid ?
					(1.0f - sunFraction) * direct[c] :
					(1.0f - sunFraction) * MAX(0.0f, direct[c] - ambient[c]);
				d = Com_Clamp(0.0f, rest, d);
			}
			directed[c] = d;
			isotropic[c] = rest - d;
		}

		const float dirLuma = 0.2126f * directed[0] + 0.7152f * directed[1] + 0.0722f * directed[2];
		if ( validCell )
		{
			// the light of an average place of the map (r_particleLighting reference)
			referenceSum += 0.2126f * total[0] + 0.7152f * total[1] + 0.0722f * total[2];
			numReferenceCells++;
		}
		if ( dirLuma > 0.0f )
			numDirCells++;

		staticData[i * 4 + 0] = FloatToHalf(isotropic[0]);
		staticData[i * 4 + 1] = FloatToHalf(isotropic[1]);
		staticData[i * 4 + 2] = FloatToHalf(isotropic[2]);
		for ( int c = 0; c < 3; c++ )
		{
			dirData[i * 4 + c] = FloatToHalf(directed[c]);
			dirVecData[i * 4 + c] = FloatToHalf(cellDir[c] * dirLuma);
		}
		dirData[i * 4 + 3] = FloatToHalf(dirLuma);
		dirVecData[i * 4 + 3] = FloatToHalf(0.0f);
		float trust = 1.0f;
		if ( traceTrust && (sun[0] > 0.0f || sun[1] > 0.0f || sun[2] > 0.0f) )
		{
			trust = R_VolumetricSunTrust(world, i, sunDir);
			numTraced++;
			if ( trust > 0.0f )
				numTrusted++;
		}
		staticData[i * 4 + 3] = FloatToHalf(trust);

		sunData[i * 4 + 0] = FloatToHalf(sun[0]);
		sunData[i * 4 + 1] = FloatToHalf(sun[1]);
		sunData[i * 4 + 2] = FloatToHalf(sun[2]);
		sunData[i * 4 + 3] = FloatToHalf(1.0f);

		// reconstruction error of the stored parts against the legacy value
		for ( int c = 0; c < 3; c++ )
		{
			const float stored = R_VolumetricHalfToFloat(staticData[i * 4 + c]) +
				R_VolumetricHalfToFloat(dirData[i * 4 + c]) + R_VolumetricHalfToFloat(sunData[i * 4 + c]);
			const float error = fabsf(stored - total[c]);
			maxError = MAX(maxError, error);
			sumError += error;
			if ( total[c] > 1e-3f )
				maxRelError = MAX(maxRelError, error / total[c]);
		}

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
		GL_R11F_G11F_B10F);	// rgb only: half the size and bandwidth
	world->volumetricDirGrid = R_CreateImage3D(
		"*volumetricDirGrid", (byte *)dirData,
		world->lightGridBounds[0], world->lightGridBounds[1], world->lightGridBounds[2],
		GL_RGBA16F);	// half floats: I + D + B must match the legacy grid
	world->volumetricDirVecGrid = R_CreateImage3D(
		"*volumetricDirVecGrid", (byte *)dirVecData,
		world->lightGridBounds[0], world->lightGridBounds[1], world->lightGridBounds[2],
		GL_RGBA16F);

	if ( numReferenceCells > 0 )
		world->particleLightReference = (float)(referenceSum / numReferenceCells);

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
	ri.Printf(PRINT_DEVELOPER, "Froxel fog sun trust: %d cells traced, %d see the sun, %d msec\n",
		numTraced, numTrusted, ri.Milliseconds() - traceStart);

	// the sun part is counted as half here, its R11G11B10F texture adds its
	// own rounding on the GPU (debug view 25)
	ri.Printf(PRINT_DEVELOPER, "Froxel fog directed light grid: %d cells, reconstruction error max %g (%.3f%%), mean %g\n",
		numDirCells, maxError, maxRelError * 100.0f, sumError / (3.0f * numCells));

	Z_Free(dirVecData);
	Z_Free(dirData);
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

// slice of the view depth d (inverse of R_VolumetricSliceDistance)
static int R_VolumetricDepthSlice( float d )
{
	const float firstBoundary = R_VolumetricSliceDistance(1, s_vf.nearZ, s_vf.farZ, s_vf.depth);
	if ( d <= firstBoundary )
		return 0;
	const int k = (int)floorf((float)s_vf.depth * logf(d / s_vf.nearZ) / logf(s_vf.farZ / s_vf.nearZ));
	return Com_Clampi(0, s_vf.depth - 1, k);
}

/*
=================
R_VolumetricBuildLightLists

Dynamic lights of the froxels, clustered like Forward+ (tr_forwardplus.cpp)
but on the froxel grid: tiles of FROXEL_LIGHT_TILE x FROXEL_LIGHT_TILE froxels,
one cluster per tile and slice. Every point light of the scene (Forward+: all
of them, most important first; legacy: the MAX_DLIGHTS of the Lights block) is
binned into the clusters its sphere may touch, at most FROXEL_LIGHTS_PER_CLUSTER
per cluster (the least important drop out). Two buffer textures per frame:

  lights  RGBA32F  FROXEL_LIGHT_TEXELS per light: origin, radius | color, shadow cube layer
                   (-1 none) | spot axis, cos outer (-2: point) | cos inner (-1: point),
                   projected spot shadow, cookie layer (-1 none), cookie roll
                   (tr_spotlight.cpp, tr_lightcookie.cpp)
  list    R32UI    one header per cluster (first entry | count << 24), then the
                   light indexes
=================
*/
#define FROXEL_LIGHT_TILE			8
#define FROXEL_LIGHTS_PER_CLUSTER	32
#define FROXEL_LIGHT_TEXELS			4	// must match volumetric_inject.glsl / volumetric_debug.glsl

struct froxelLightRange_t
{
	int light;		// index into the lights buffer
	int x0, x1, y0, y1, z0, z1;
};

static qboolean R_VolumetricLightRange( const viewParms_t *view, const float *froxelProjection,
	const dlight_t *dl, int tilesX, int tilesY, froxelLightRange_t *range )
{
	if ( dl->radius <= 0.0f )
		return qfalse;
	// spot lights: the sphere around the cone (tr_spotlight.cpp)
	vec3_t center;
	float radius;
	R_SpotBoundingSphere(dl, center, &radius);

	// frustum sides
	for ( int p = 0; p < 4; p++ )
	{
		const cplane_t *plane = &view->frustum[p];
		if ( DotProduct(center, plane->normal) - plane->dist < -radius )
			return qfalse;
	}

	const float *mv = view->world.modelViewMatrix;
	float eye[3];
	for ( int r = 0; r < 3; r++ )
		eye[r] = mv[r] * center[0] + mv[4 + r] * center[1] + mv[8 + r] * center[2] + mv[12 + r];
	const float depth = -eye[2];
	if ( depth + radius <= 0.0f || depth - radius >= s_vf.farZ )
		return qfalse;

	range->z0 = R_VolumetricDepthSlice(depth - radius);
	range->z1 = R_VolumetricDepthSlice(depth + radius);
	range->x0 = 0;
	range->x1 = tilesX - 1;
	range->y0 = 0;
	range->y1 = tilesY - 1;

	// the projected bounding box of the sphere, the whole view once it reaches
	// the camera plane
	if ( depth - radius <= 1.0f )
		return qtrue;

	const float *P = froxelProjection;
	float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
	for ( int c = 0; c < 8; c++ )
	{
		const float x = eye[0] + ((c & 1) ? radius : -radius);
		const float y = eye[1] + ((c & 2) ? radius : -radius);
		const float z = eye[2] + ((c & 4) ? radius : -radius);
		const float cx = P[0] * x + P[4] * y + P[8] * z + P[12];
		const float cy = P[1] * x + P[5] * y + P[9] * z + P[13];
		const float cw = P[3] * x + P[7] * y + P[11] * z + P[15];
		if ( cw <= 1e-4f )
			return qtrue;
		minX = MIN(minX, cx / cw);
		maxX = MAX(maxX, cx / cw);
		minY = MIN(minY, cy / cw);
		maxY = MAX(maxY, cy / cw);
	}
	if ( maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f )
		return qfalse;

	const float tileX = (float)(s_vf.width) / (float)FROXEL_LIGHT_TILE;
	const float tileY = (float)(s_vf.height) / (float)FROXEL_LIGHT_TILE;
	range->x0 = Com_Clampi(0, tilesX - 1, (int)floorf((minX * 0.5f + 0.5f) * tileX));
	range->x1 = Com_Clampi(0, tilesX - 1, (int)floorf((maxX * 0.5f + 0.5f) * tileX));
	range->y0 = Com_Clampi(0, tilesY - 1, (int)floorf((minY * 0.5f + 0.5f) * tileY));
	range->y1 = Com_Clampi(0, tilesY - 1, (int)floorf((maxY * 0.5f + 0.5f) * tileY));
	return qtrue;
}

static void R_VolumetricBuildLightLists( VolumetricFogBlock *block, const viewParms_t *view,
	const trRefdef_t *refdef, const float *froxelProjection )
{
	block->lightTileSize = 0;
	block->lightTilesX = 0;
	block->lightTilesY = 0;
	VectorSet4(block->selfShadowLights, -1.0f, -1.0f, -1.0f, -1.0f);
	s_vfl.hasLights = qfalse;

	if ( !s_vfl.lightBuffers[0] || r_volumetricFogDlightScale->value <= 0.0f )
		return;

	int lightIndexes[MAX_RENDER_DLIGHTS];
	int shadowLayers[MAX_RENDER_DLIGHTS];
	const int numSceneLights = R_GetDlightList(refdef, lightIndexes, shadowLayers, MAX_RENDER_DLIGHTS);
	if ( numSceneLights <= 0 )
		return;

	const int tilesX = (s_vf.width + FROXEL_LIGHT_TILE - 1) / FROXEL_LIGHT_TILE;
	const int tilesY = (s_vf.height + FROXEL_LIGHT_TILE - 1) / FROXEL_LIGHT_TILE;
	const int numClusters = tilesX * tilesY * s_vf.depth;

	// lights touching the volume, in importance order
	static vec4_t lightData[MAX_RENDER_DLIGHTS * FROXEL_LIGHT_TEXELS];
	static froxelLightRange_t ranges[MAX_RENDER_DLIGHTS];
	const qboolean cookiesActive = R_LightCookiesActive();
	int numLights = 0;
	for ( int i = 0; i < numSceneLights; i++ )
	{
		const dlight_t *dl = refdef->dlights + lightIndexes[i];
		froxelLightRange_t *range = &ranges[numLights];
		if ( !R_VolumetricLightRange(view, froxelProjection, dl, tilesX, tilesY, range) )
			continue;
		range->light = numLights;
		// spot lights without shadow (SPOTLIGHT_NOSHADOW, r_spotLightShadows 0) and
		// the legacy cube index of a light that has none
		const int shadowLayer = R_DlightCastsShadow(dl) ? shadowLayers[i] : -1;
		const float projected = (shadowLayer >= 0 && dl->spotShadowSlot == shadowLayer) ? 1.0f : 0.0f;
		float *t = lightData[numLights * FROXEL_LIGHT_TEXELS];
		VectorSet4(t + 0, dl->origin[0], dl->origin[1], dl->origin[2], dl->radius);
		VectorSet4(t + 4, dl->color[0], dl->color[1], dl->color[2], (float)shadowLayer);
		VectorSet4(t + 8, dl->spotDir[0], dl->spotDir[1], dl->spotDir[2], dl->spotCosOuter);
		VectorSet4(t + 12, dl->spotCosInner, projected,
			cookiesActive ? (float)dl->cookieLayer : -1.0f, dl->cookieRoll);
		numLights++;
	}
	if ( !numLights )
		return;

	// media self-shadow of the dynamic lights (r_volumetricSelfShadow 2): only
	// the strongest few march through the media, scored by the light arriving
	// near the camera, luminance * radius^2 / max(distance, radius / 4)^2
	if ( tr.froxelMediaImage && r_volumetricSelfShadow->integer >= 2 )
	{
		const int maxShadowed = Com_Clampi(0, 4, r_volumetricSelfShadowMaxLights->integer);
		float best[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for ( int n = 0; n < numLights; n++ )
		{
			const float *t = lightData[n * FROXEL_LIGHT_TEXELS];
			const float radius = t[3];
			const float luminance = 0.2126f * t[4] + 0.7152f * t[5] + 0.0722f * t[6];
			const float dist = Q_max(Distance(t, view->ori.origin), 0.25f * radius);
			const float score = luminance * radius * radius / Q_max(dist * dist, 1.0f);
			if ( score <= 0.0f )
				continue;
			for ( int k = 0; k < maxShadowed; k++ )
			{
				if ( score > best[k] )
				{
					for ( int m = maxShadowed - 1; m > k; m-- )
					{
						best[m] = best[m - 1];
						block->selfShadowLights[m] = block->selfShadowLights[m - 1];
					}
					best[k] = score;
					block->selfShadowLights[k] = (float)n;
					break;
				}
			}
		}
	}

	// pass 1: counts, pass 2: fill (same order and cap)
	s_vfl.lightCounts.assign(numClusters, 0);
	for ( int n = 0; n < numLights; n++ )
	{
		const froxelLightRange_t& r = ranges[n];
		for ( int z = r.z0; z <= r.z1; z++ )
			for ( int y = r.y0; y <= r.y1; y++ )
			{
				uint32_t *row = &s_vfl.lightCounts[(z * tilesY + y) * tilesX];
				for ( int x = r.x0; x <= r.x1; x++ )
				{
					if ( row[x] < FROXEL_LIGHTS_PER_CLUSTER )
						row[x]++;
				}
			}
	}

	uint32_t total = 0;
	s_vfl.lightList.resize(numClusters);
	for ( int c = 0; c < numClusters; c++ )
	{
		s_vfl.lightList[c] = (uint32_t)numClusters + total;	// absolute first entry
		total += s_vfl.lightCounts[c];
	}
	if ( numClusters + total >= (1u << 24) )
		return;
	s_vfl.lightList.resize(numClusters + total);

	s_vfl.lightCursor.assign(numClusters, 0);
	for ( int n = 0; n < numLights; n++ )
	{
		const froxelLightRange_t& r = ranges[n];
		for ( int z = r.z0; z <= r.z1; z++ )
			for ( int y = r.y0; y <= r.y1; y++ )
			{
				const int rowStart = (z * tilesY + y) * tilesX;
				for ( int x = r.x0; x <= r.x1; x++ )
				{
					const int c = rowStart + x;
					if ( s_vfl.lightCursor[c] < s_vfl.lightCounts[c] )
						s_vfl.lightList[s_vfl.lightList[c] + s_vfl.lightCursor[c]++] = (uint32_t)r.light;
				}
			}
	}
	for ( int c = 0; c < numClusters; c++ )
		s_vfl.lightList[c] |= s_vfl.lightCounts[c] << 24;

	// this frame's buffers (orphaned: the GPU may still read the previous data)
	const int slot = backEndData->realFrameNumber % MAX_FRAMES;
	qglBindBuffer(GL_TEXTURE_BUFFER, s_vfl.lightBuffers[slot]);
	qglBufferData(GL_TEXTURE_BUFFER, numLights * FROXEL_LIGHT_TEXELS * sizeof(vec4_t), lightData, GL_STREAM_DRAW);
	qglBindBuffer(GL_TEXTURE_BUFFER, s_vfl.listBuffers[slot]);
	qglBufferData(GL_TEXTURE_BUFFER, s_vfl.lightList.size() * sizeof(uint32_t), s_vfl.lightList.data(), GL_STREAM_DRAW);
	qglBindBuffer(GL_TEXTURE_BUFFER, 0);

	s_vfl.lightSlot = slot;
	s_vfl.hasLights = qtrue;
	block->lightTileSize = FROXEL_LIGHT_TILE;
	block->lightTilesX = tilesX;
	block->lightTilesY = tilesY;
}

/*
=================
R_VolumetricHeightFog

Height fog medium (r_volumetricFogHeight 1, off by default), world anchored:

  sigma(p) = sigma0 * min(exp(-(p.z - base) / falloff), maxScale) * cutoff
  sigma0   = -ln(1.5 / 255) / r_volumetricFogHeightOpaqueDistance * volumetricFogScale

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

	const float opaque = r_volumetricFogHeightOpaqueDistance->value;
	if ( !r_volumetricFogHeight->integer || opaque <= 0.0f )
		return qfalse;

	const float extinction = (-logf(1.5f / 255.0f)) / opaque *
		tr.volumetricFogScale * r_volumetricFogScale->value;
	if ( extinction <= 0.0f )
		return qfalse;

	const float falloff = MAX(1.0f, r_volumetricFogHeightFalloff->value);
	const float maxScale = MAX(1.0f, r_volumetricFogHeightMaxDensity->value);
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
	const float topHeight = MAX(0.0f, r_volumetricFogHeightTopHeight->value);
	VectorSet4(color, albedo[0], albedo[1], albedo[2], topHeight - MIN(falloff, topHeight));

	// relative extinction per channel (r_volumetricFogRGBExtinction), mean 1
	vec3_t extinctionColor = { 1.0f, 1.0f, 1.0f };
	sscanf(r_volumetricFogHeightExtinction->string, "%f %f %f",
		&extinctionColor[0], &extinctionColor[1], &extinctionColor[2]);
	R_VolumetricExtinctionColor(extinctionColor, extinctionColor);
	VectorSet4(top, topHeight, extinctionColor[0], extinctionColor[1], extinctionColor[2]);
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
	ri.Printf(PRINT_ALL, "  opaque  %g\n", r_volumetricFogHeightOpaqueDistance->value);
	ri.Printf(PRINT_ALL, "  falloff %g\n", r_volumetricFogHeightFalloff->value);
	ri.Printf(PRINT_ALL, "  color   %s\n", r_volumetricFogHeightColor->string);
	ri.Printf(PRINT_ALL, "  base    %g\n", r_volumetricFogHeightBase->value);
	ri.Printf(PRINT_ALL, "  top     %g\n", r_volumetricFogHeightTopHeight->value);
	ri.Printf(PRINT_ALL, "  max     %g\n", r_volumetricFogHeightMaxDensity->value);

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
			r_volumetricFogHeight, r_volumetricFogHeightOpaqueDistance, r_volumetricFogHeightFalloff,
			r_volumetricFogHeightColor, r_volumetricFogHeightTopHeight, r_volumetricFogHeightMaxDensity };
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

		ri.Cvar_Set("r_volumetricFogHeightOpaqueDistance", va("%g", opaque));
		ri.Cvar_Set("r_volumetricFogHeightFalloff", "65536");
		ri.Cvar_Set("r_volumetricFogHeightTopHeight", "0");
		ri.Cvar_Set("r_volumetricFogHeightMaxDensity", "1");
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
		if ( !Q_stricmp(key, "opaque") ) cvar = "r_volumetricFogHeightOpaqueDistance";
		else if ( !Q_stricmp(key, "falloff") ) cvar = "r_volumetricFogHeightFalloff";
		else if ( !Q_stricmp(key, "base") ) cvar = "r_volumetricFogHeightBase";
		else if ( !Q_stricmp(key, "top") ) cvar = "r_volumetricFogHeightTopHeight";
		else if ( !Q_stricmp(key, "max") ) cvar = "r_volumetricFogHeightMaxDensity";
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

static unsigned int R_VolumetricHashBytes( unsigned int key, const void *data, size_t size )
{
	const byte *bytes = (const byte *)data;
	for ( size_t i = 0; i < size; i++ )
		key = (key ^ bytes[i]) * 16777619u;
	return key;
}

// The medium and light settings the history was built with (a change resets
// it): the radiance clamp cannot repair a history whose extinction or noise
// coordinates (the wind phase is absolute time * wind) are different.
static unsigned int R_VolumetricMediumKey( void )
{
	const float values[] = {
		(float)r_volumetricFogNoise->integer,
		r_volumetricFogNoiseScale->value,
		r_volumetricFogNoiseContrast->value,
		r_volumetricFogNoiseDetailScale->value,
		r_volumetricFogNoiseDetailContrast->value,
		r_volumetricFogScale->value,
		tr.volumetricFogScale,
		(float)r_volumetricFogHeight->integer,
		r_volumetricFogHeightOpaqueDistance->value,
		r_volumetricFogHeightBase->value,
		r_volumetricFogHeightFalloff->value,
		r_volumetricFogHeightMaxDensity->value,
		r_volumetricFogHeightTopHeight->value,
		r_volumetricFogAnisotropy->value,
		r_volumetricFogSunScale->value,
		r_volumetricFogStaticScale->value,
		(float)r_volumetricFogStaticDirectional->integer,
		// the sun octaves are in the history (r_volumetricMultiScatter)
		(float)r_volumetricMultiScatter->integer,
		(float)r_volumetricMultiScatterOctaves->integer,
		r_volumetricMultiScatterAttenuation->value,
		r_volumetricMultiScatterContribution->value,
		r_volumetricMultiScatterPhase->value,
		r_volumetricMultiScatterLength->value,
		r_volumetricMultiScatterShadowFill->value };
	unsigned int key = R_VolumetricHashBytes(2166136261u, values, sizeof(values));
	key = R_VolumetricHashBytes(key, r_volumetricFogHeightColor->string, strlen(r_volumetricFogHeightColor->string));
	key = R_VolumetricHashBytes(key, r_volumetricFogHeightExtinction->string, strlen(r_volumetricFogHeightExtinction->string));
	key = R_VolumetricHashBytes(key, r_volumetricFogNoiseWind->string, strlen(r_volumetricFogNoiseWind->string));
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
	tr.volParticlesUboOffset = -1;
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
		// no fog volume, no height fog, no local fog volume, no FX particle medium: nothing to do
		// the sprite particle light field (r_particleLighting) needs the injection even without media
		(tr.world->numfogs > 1 || heightFogOn || R_FogVolumesInFrustum(view, refdef, farZ) ||
			R_VolParticlesInFrustum(view, refdef, farZ) || tr.froxelParticleLightImage != NULL) &&
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

		// the debug views read the particle block: none while frozen
		static const VolumetricParticlesBlock noParticles = {};
		tr.volParticlesUboOffset = RB_AppendConstantsData(frame, &noParticles, sizeof(noParticles));
		return;
	}

	// history
	const int debug = r_volumetricFogDebug->integer;
	const unsigned int mediumKey = R_VolumetricMediumKey();
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
		s_vf.mediumKey == mediumKey &&
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
	s_vf.mediumKey = mediumKey;
	s_vf.frameIndex++;

	R_VolumetricBuildLightLists(&block, view, refdef, froxelProjection);

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
		MAX(r_volumetricEmission->value, 0.0f),	// emission of local volumes / FX particles
		4.0f);	// history radiance clamped to [current / 4, current * 4]

	const float globalAnisotropy = Com_Clamp(-0.9f, 0.9f, r_volumetricFogAnisotropy->value);
	VectorSet4(block.lightParams,
		globalAnisotropy,
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

	VectorSet4(block.debugParams, (float)debug, r_volumetricFogBloom->value, 0.0f,
		r_volumetricFogStaticDirectional->integer ? 1.0f : 0.0f);

	// media self-shadow: the media pass builds the extinction of this frame
	// before the injection (RB_VolumetricBuild)
	VectorSet4(block.selfShadow,
		tr.froxelMediaImage ? (float)r_volumetricSelfShadow->integer : 0.0f,
		(float)Com_Clampi(2, 16, r_volumetricSelfShadowSamples->integer),
		Com_Clamp(32.0f, 8192.0f, r_volumetricSelfShadowDistance->value),
		r_volumetricSelfShadowOutsideHeightFog->integer ? 1.0f : 0.0f);

	// approximate multiple scattering: octaves of the self-shadowed light terms,
	// so only with the media self-shadow (without an optical depth towards the
	// light there is no lost energy to return). a <= b (Hillaire 2016): an octave
	// never gains more from the lower attenuation than it loses by its weight.
	{
		static qboolean warnedNoSelfShadow = qfalse;
		static qboolean warnedAttenuation = qfalse;
		int msMode = r_volumetricMultiScatter->integer;
		if ( msMode && !block.selfShadow[0] )
		{
			if ( !warnedNoSelfShadow )
				ri.Printf(PRINT_ALL, "r_volumetricMultiScatter needs r_volumetricSelfShadow (vid_restart), ignored\n");
			warnedNoSelfShadow = qtrue;
			msMode = 0;
		}
		// the dynamic light octaves only exist where their media shadow does
		msMode = MIN(msMode, (int)block.selfShadow[0]);
		const float b = Com_Clamp(0.0f, 1.0f, r_volumetricMultiScatterContribution->value);
		float a = Com_Clamp(0.0f, 1.0f, r_volumetricMultiScatterAttenuation->value);
		if ( a > b )
		{
			if ( !warnedAttenuation )
				ri.Printf(PRINT_ALL, "r_volumetricMultiScatterAttenuation %g > r_volumetricMultiScatterContribution %g: clamped to %g (energy)\n", a, b, b);
			warnedAttenuation = qtrue;
			a = b;
		}
		VectorSet4(block.multiScatter, (float)msMode, (float)Com_Clampi(1, 3, r_volumetricMultiScatterOctaves->integer), b,
			Com_Clamp(1.0f, 4096.0f, r_volumetricMultiScatterLength->value));
		VectorSet4(block.multiScatter2, a, Com_Clamp(0.0f, 1.0f, r_volumetricMultiScatterPhase->value),
			Com_Clamp(0.0f, 0.5f, r_volumetricMultiScatterShadowFill->value), 0.0f);
	}

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
		// per fog medium (fogAlbedo / fogAnisotropy shader keywords), else the legacy
		// fog color and the global r_volumetricFogAnisotropy
		if ( fog->parms.hasAlbedo )
		{
			VectorSet4(block.fogColor[i], fog->parms.albedo[0] * tr.identityLight,
				fog->parms.albedo[1] * tr.identityLight, fog->parms.albedo[2] * tr.identityLight, extinction);
		}
		else
			VectorSet4(block.fogColor[i], fog->color[0], fog->color[1], fog->color[2], extinction);
		// fogExtinctionColor (r_volumetricFogRGBExtinction), mean 1; neutral without it
		vec3_t extinctionColor;
		R_VolumetricExtinctionColor(fog->parms.hasExtinctionColor ? fog->parms.extinctionColor : NULL,
			extinctionColor);
		VectorSet4(block.fogMedium[i], fog->parms.hasAnisotropy ? fog->parms.anisotropy : globalAnisotropy,
			extinctionColor[0], extinctionColor[1], extinctionColor[2]);
		VectorCopy4(fog->surface, block.fogPlane[i]);
		VectorSet4(block.fogMins[i], fog->bounds[0][0], fog->bounds[0][1], fog->bounds[0][2], fog->hasSurface ? 1.0f : 0.0f);
		const qboolean noisy = (qboolean)(noiseMask & ((fog == tr.world->globalFog) ? 4 : 2));
		VectorSet4(block.fogMaxs[i], fog->bounds[1][0], fog->bounds[1][1], fog->bounds[1][2], noisy ? 1.0f : 0.0f);
	}

	// the slices each fog volume may touch: its bounds against the frustum
	// sides and its view depth range, one slice wider on both sides (the
	// injection samples at jittered positions, up to half a slice away)
	for ( int i = 0; i < numFogs; i++ )
	{
		const fog_t *fog = tr.world->fogs + i + 1;
		qboolean visible = qtrue;
		for ( int p = 0; p < 4 && visible; p++ )
		{
			// the corner of the bounds furthest along the plane normal
			const cplane_t *plane = &view->frustum[p];
			vec3_t corner;
			for ( int c = 0; c < 3; c++ )
				corner[c] = fog->bounds[plane->normal[c] >= 0.0f ? 1 : 0][c];
			if ( DotProduct(corner, plane->normal) - plane->dist < 0.0f )
				visible = qfalse;
		}
		if ( !visible )
			continue;

		float minDepth = 1e30f, maxDepth = -1e30f;
		for ( int c = 0; c < 8; c++ )
		{
			const vec3_t corner = {
				fog->bounds[c & 1][0], fog->bounds[(c >> 1) & 1][1], fog->bounds[(c >> 2) & 1][2] };
			vec3_t delta;
			VectorSubtract(corner, view->ori.origin, delta);
			const float depth = DotProduct(delta, forward);
			minDepth = MIN(minDepth, depth);
			maxDepth = MAX(maxDepth, depth);
		}
		if ( maxDepth < 0.0f || minDepth > farZ )
			continue;

		const int z0 = Q_max(0, R_VolumetricDepthSlice(minDepth) - 1);
		const int z1 = Q_min(s_vf.depth - 1, R_VolumetricDepthSlice(maxDepth) + 1);
		for ( int k = z0; k <= z1; k++ )
			block.fogSlices[k] |= 1 << i;
	}

	// local fog volumes: culled, nearest first, per slice lists (tr_fogvolume.cpp).
	// Like the height fog they are outside the BSP fog volumes: transparent
	// surfaces without a fog volume look the volume up too.
	const int numLocalVolumes = R_FogVolumesBuild(&block, view, refdef, forward, nearZ, farZ, s_vf.depth,
		(noiseMask & 8) ? qtrue : qfalse);

	// FX particle media: culled, capped, per slice lists (tr_volparticle.cpp),
	// in their own block (the VolumetricFog block is near the UBO minimum)
	static VolumetricParticlesBlock particles;
	const int numParticles = R_VolParticlesBuild(&particles, view, refdef, forward, nearZ, farZ, s_vf.depth);
	tr.volParticlesUboOffset = RB_AppendConstantsData(frame, &particles, sizeof(particles));

	s_vf.frameHeightFog = (qboolean)(heightFogOn || numLocalVolumes > 0 || numParticles > 0);

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
	// The main view state survives into HUD and menu draws. Screen-space
	// UI must not inherit its froxel fog or the height-fog surface path.
	if ( backEnd.projection2D || !backEnd.volumetricView || backEnd.depthFill || backEnd.refractionFill )
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

	// RGB extinction: the lookup of T.rgb (u_FroxelFogMode 3)
	if ( mode == 1 && s_vf.rgb )
		mode = 3;
	uniforms.SetUniformInt(UNIFORM_FROXELFOGMODE, mode);
	if ( mode == 1 || mode == 3 )
	{
		samplers.AddStaticImage(tr.froxelIntegratedImage, TB_CUBEMAP);
		samplers.AddStaticImage(tr.froxelTailImage, TB_ENVBRDFMAP);
	}
	if ( mode == 3 )
		samplers.AddStaticImage(tr.froxelTransmittanceImage, TB_FROXELTRANSMITTANCE);
}

/*
=================
RB_VolumetricSetupFogPassDraw

The fog pass (RB_FogPass) blends one color over the surface: the scalar
lookup outputs (S, 1 - T) for ONE, ONE_MINUS_SRC_ALPHA. With RGB extinction
it is drawn twice: rgbPass 1 the transmittance (u_FroxelFogMode 3, blend
ZERO, SRC_COLOR), rgbPass 2 the in-scattering (4, ONE, ONE). Call after
RB_VolumetricSetupFogDraw (overrides its mode).
=================
*/
void RB_VolumetricSetupFogPassDraw( int rgbPass, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers )
{
	if ( !s_vf.rgb || rgbPass <= 0 )
		return;
	uniforms.SetUniformInt(UNIFORM_FROXELFOGMODE, rgbPass == 1 ? 3 : 4);
}

/*
=================
RB_ParticleLightClass

Sprite particle lighting (r_particleLighting): which FX sprite stages are lit by
the particle light field. From the shader state only, no names:
- the entity is a sprite (RT_SPRITE: CParticle, CFlash) or an oriented quad
  (RT_ORIENTED_QUAD: COrientedParticle), not RF_VOLUMETRIC / first person
- lit: alpha blending, GL_SRC_ALPHA or GL_ONE (premultiplied) over
  GL_ONE_MINUS_SRC_ALPHA: smoke, dust, steam. The color is a reflectance.
- unlit: everything else, additive (GL_ONE / GL_SRC_ALPHA over GL_ONE: sparks,
  flashes, glows, fire) and modulate (darkening filters). An additive sprite
  emits light, lighting it would grey it out where there is no light.
- unlit as well: glow stages, rgbGen lightingDiffuse (already lit)
- the shader keyword particleLighting on / off overrides the blend test
=================
*/
int RB_ParticleLightClass( const shader_t *shader, const shaderStage_t *stage )
{
	const trRefEntity_t *ent = backEnd.currentEntity;
	if ( !ent || ent == &tr.worldEntity || !stage || !shader )
		return PARTICLE_LIGHT_NONE;
	if ( ent->e.reType != RT_SPRITE && ent->e.reType != RT_ORIENTED_QUAD )
		return PARTICLE_LIGHT_NONE;
	if ( ent->e.renderfx & (RF_VOLUMETRIC | RF_FIRST_PERSON) )
		return PARTICLE_LIGHT_NONE;

	if ( shader->particleLight < 0 )
		return PARTICLE_LIGHT_UNLIT;
	if ( stage->glow ||
		stage->rgbGen == CGEN_LIGHTING_DIFFUSE ||
		stage->rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY )
		return PARTICLE_LIGHT_UNLIT;
	if ( shader->particleLight > 0 )
		return PARTICLE_LIGHT_LIT;

	const uint32_t src = stage->stateBits & GLS_SRCBLEND_BITS;
	const uint32_t dst = stage->stateBits & GLS_DSTBLEND_BITS;
	if ( dst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA &&
		(src == GLS_SRCBLEND_SRC_ALPHA || src == GLS_SRCBLEND_ONE) )
		return PARTICLE_LIGHT_LIT;
	return PARTICLE_LIGHT_UNLIT;
}

// the field of this frame can be looked up by this draw
static qboolean RB_ParticleLightActive( const shader_t *shader )
{
	return (qboolean)(
		tr.froxelParticleLightImage != NULL &&
		s_vf.built &&
		r_particleLightingMix->value > 0.0f &&
		RB_VolumetricFogMode(shader->sort) == 1);
}

/*
=================
RB_ParticleLightNeedsFogProgram

The lookup is in the froxel fog code of generic.glsl (USE_FOG): lit sprites
(and, with r_particleLightingDebug 5, the unlit ones) use the fog permutation
even outside of fog. Their fog mode is then 2 (none) unless they are fogged.
=================
*/
qboolean RB_ParticleLightNeedsFogProgram( const shader_t *shader, const shaderStage_t *stage )
{
	if ( !RB_ParticleLightActive(shader) )
		return qfalse;
	const int particleClass = RB_ParticleLightClass(shader, stage);
	if ( particleClass == PARTICLE_LIGHT_LIT )
		return qtrue;
	return (qboolean)(particleClass == PARTICLE_LIGHT_UNLIT && r_particleLightingDebug->integer == 5);
}

/*
=================
RB_ParticleLightSetupDraw

u_ParticleLight of the generic programs, set for every generic stage (the
value stays in the program): x = gain (r_particleLightingScale over the map
average light, so that an average place keeps the authored color), y = floor,
z = max gain, w = mix (0 = off; 2 / 3 = r_particleLightingDebug 5 tint of a
lit / unlit sprite).
=================
*/
void RB_ParticleLightSetupDraw( const shader_t *shader, const shaderStage_t *stage,
	UniformDataWriter& uniforms, SamplerBindingsWriter& samplers )
{
	if ( !s_vf.resources )
		return;

	vec4_t params = { 0.0f, 0.0f, 0.0f, 0.0f };
	if ( RB_ParticleLightNeedsFogProgram(shader, stage) )
	{
		const int particleClass = RB_ParticleLightClass(shader, stage);
		if ( r_particleLightingDebug->integer == 5 )
		{
			params[3] = (particleClass == PARTICLE_LIGHT_LIT) ? 2.0f : 3.0f;
		}
		else
		{
			// the field carries the baked light times r_volumetricFogStaticScale
			const float reference = MAX(0.05f,
				(tr.world ? tr.world->particleLightReference : 0.0f) * r_volumetricFogStaticScale->value);
			params[0] = r_particleLightingScale->value / reference;
			params[1] = r_particleLightingFloor->value;
			params[2] = 4.0f;
			params[3] = r_particleLightingMix->value;
		}
		samplers.AddStaticImage(tr.froxelParticleLightImage, TB_SHADOWMAPARRAY);
	}
	uniforms.SetUniformVec4(UNIFORM_PARTICLELIGHT, params);
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

	// FX particle media, read by the injection and the debug views
	if ( tr.volParticlesUboOffset != -1 )
		RB_BindUniformBlock(frameUbo, UNIFORM_BLOCK_VOLUMETRIC_PARTICLES, tr.volParticlesUboOffset);
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
	const bool compute = s_vf.computeAvailable && tr.volumetricInjectComputeShader.program &&
		tr.volumetricIntegrateComputeShader.program &&
		R_HasModernFeatures(MODERN_COMPUTE | MODERN_IMAGE_LOAD_STORE);
	s_vf.builtVolumeFrame = s_vf.volumeFrameNumber;
	s_vf.builtVolumeImage = current;

	R_PushDebugGroup(AL_STAGE, "Froxel fog");
	GL_Cull(CT_TWO_SIDED);
	GL_State(GLS_DEPTHTEST_DISABLE);
	RB_VolumetricBindBlocks();
	if ( compute )
	{
		// Order image overwrites after texture reads from the previous frame.
		qglMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		FBO_Bind(NULL);
	}
	auto bindOutput = []( GLuint unit, image_t *image, GLenum format, bool layered )
	{
		qglBindImageTexture(unit, image ? image->texnum : 0, 0,
			layered ? GL_TRUE : GL_FALSE, 0, GL_WRITE_ONLY, format);
	};
	auto dispatchInject = [&]()
	{
		qglDispatchCompute((s_vf.width + 3) / 4, (s_vf.height + 3) / 4, (s_vf.depth + 3) / 4);
		qglMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	};

	// media (r_volumetricSelfShadow): the extinction of this frame at the
	// froxel centers, no jitter and no history, so the light rays of the
	// injection see the current density (moving smoke) of every froxel. The
	// injection program in its media mode (u_ParticleLight.z), all slices in
	// one layered draw.
	int timer;
	if ( tr.froxelMediaFbo )
	{
		timer = RB_VolumetricBeginTimer("Froxel fog media");
		shaderProgram_t *sp = compute ? &tr.volumetricInjectComputeShader : &tr.volumetricInjectShader;
		if ( !compute )
			FBO_Bind(tr.froxelMediaFbo);
		GL_SetViewportAndScissor(0, 0, s_vf.width, s_vf.height);
		GLSL_BindProgram(sp);
		GL_BindToTMU(tr.froxelNoiseImage, TB_DELUXEMAP);
		const vec4_t mediaMode = { 0.0f, 0.0f, 1.0f, 0.0f };
		GLSL_SetUniformVec4(sp, UNIFORM_PARTICLELIGHT, mediaMode);
		GLSL_SetUniformInt(sp, UNIFORM_FROXELSLICE, 0);
		if ( compute )
		{
			bindOutput(5, tr.froxelMediaImage, GL_R16F, true);
			dispatchInject();
		}
		else
			qglDrawArraysInstanced(GL_TRIANGLES, 0, 3, s_vf.depth);
		RB_VolumetricEndTimer(timer);
	}

	// Injection + temporal filter: one 3D dispatch or one layered draw.
	timer = RB_VolumetricBeginTimer("Froxel fog inject");
	{
		shaderProgram_t *sp = compute ? &tr.volumetricInjectComputeShader : &tr.volumetricInjectShader;
		if ( !compute )
			FBO_Bind(tr.froxelInjectFbo);
		GL_SetViewportAndScissor(0, 0, s_vf.width, s_vf.height);
		GLSL_BindProgram(sp);

		image_t *staticGrid = tr.world->volumetricStaticGrid ? tr.world->volumetricStaticGrid : tr.whiteImage3D;
		image_t *sunGrid = tr.world->volumetricSunGrid ? tr.world->volumetricSunGrid : tr.whiteImage3D;
		if ( !tr.world->volumetricStaticGrid && tr.world->volumetricLightMaps[0] )
			staticGrid = tr.world->volumetricLightMaps[0];
		// without the split the static grid holds everything: nothing is directed
		image_t *dirGrid = tr.world->volumetricDirGrid ? tr.world->volumetricDirGrid : tr.blackImage3D;
		image_t *dirVecGrid = tr.world->volumetricDirVecGrid ? tr.world->volumetricDirVecGrid : tr.blackImage3D;
		image_t *legacyGrid = tr.world->volumetricLightMaps[0] ? tr.world->volumetricLightMaps[0] : tr.blackImage3D;

		GL_BindToTMU(tr.froxelInjectImage[previous], TB_COLORMAP);
		GL_BindToTMU(tr.froxelNoiseImage, TB_DELUXEMAP);
		GL_BindToTMU(staticGrid, TB_LIGHTMAP);
		GL_BindToTMU(sunGrid, TB_NORMALMAP);
		GL_BindToTMU(dirGrid, TB_SPECULARMAP);
		GL_BindToTMU(dirVecGrid, TB_SSAOMAP);
		GL_BindToTMU(legacyGrid, TB_EMISSIVEMAP);
		if ( tr.sunShadowArrayImage )
			GL_BindToTMU(tr.sunShadowArrayImage, TB_SHADOWMAP);
		if ( tr.pointShadowArrayImage )
			GL_BindToTMU(tr.pointShadowArrayImage, TB_SHADOWMAPARRAY);
		if ( s_vfl.hasLights )
		{
			GL_BindToTMU(&s_vfl.lightImages[s_vfl.lightSlot], TB_FPLUS_LIGHTS);
			GL_BindToTMU(&s_vfl.listImages[s_vfl.lightSlot], TB_FPLUS_GRID);
		}
		if ( tr.froxelMediaImage )
			GL_BindToTMU(tr.froxelMediaImage, TB_FROXELMEDIA);
		// RGB extinction: the history of sigma_t.rgb
		if ( s_vf.rgb )
			GL_BindToTMU(tr.froxelExtinctionImage[previous], TB_FROXELEXTINCTION);

		// every slice: instance k renders layer k
		GLenum bufs[4];
		const int numBufs = R_VolumetricInjectDrawBuffers(bufs);
		if ( !compute )
		{
			qglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tr.froxelInjectImage[current]->texnum, 0);
			qglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, tr.froxelDynamicImage->texnum, 0);
			if ( tr.froxelParticleLightImage )
				qglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, tr.froxelParticleLightImage->texnum, 0);
			if ( s_vf.rgb )
				qglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, tr.froxelExtinctionImage[current]->texnum, 0);
			// draw buffers 2 and up are color masked by default with SSR / SSGI (GL_ResetScreenAuxWrite)
			if ( numBufs > 2 )
				GL_SetScreenAuxWrite(true);
			qglDrawBuffers(numBufs, bufs);
		}
		{
			// sprite particle light field: on, debug term (r_particleLightingDebug 2-4)
			const int term = r_particleLightingDebug->integer;
			vec4_t particleLight;
			VectorSet4(particleLight, tr.froxelParticleLightImage ? 1.0f : 0.0f,
				(term >= 2 && term <= 4) ? (float)term : 0.0f, 0.0f, 0.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_PARTICLELIGHT, particleLight);
		}
		{
			// spot light cookies (tr_lightcookie.cpp): lod from the world size of a froxel
			vec4_t cookie;
			R_LightCookieParams(2.0f * tanf(DEG2RAD(backEnd.viewParms.fovY * 0.5f)) / (float)s_vf.height, cookie);
			GLSL_SetUniformVec4(sp, UNIFORM_LIGHTCOOKIEPARAMS, cookie);
			if ( cookie[0] > 0.0f )
				GL_BindToTMU(R_LightCookieImage(), TB_LIGHTCOOKIES);
		}
		GLSL_SetUniformInt(sp, UNIFORM_FROXELSLICE, 0);
		if ( compute )
		{
			bindOutput(0, tr.froxelInjectImage[current], GL_RGBA16F, true);
			bindOutput(1, tr.froxelDynamicImage, GL_R11F_G11F_B10F, true);
			bindOutput(2, tr.froxelParticleLightImage, GL_R11F_G11F_B10F, true);
			bindOutput(3, tr.froxelExtinctionImage[current], GL_RGBA16F, true);
			bindOutput(4, tr.froxelTailImage, GL_RGBA16F, false);
			dispatchInject();
		}
		else
		{
			qglDrawArraysInstanced(GL_TRIANGLES, 0, 3, s_vf.depth);
			if ( numBufs > 2 )
				GL_ResetScreenAuxWrite();
			if ( tr.froxelParticleLightImage )
				qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, 0, 0);
			if ( s_vf.rgb )
				qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, 0, 0);

			// tail pass: the light at the far side of the volume (FroxelLookup
			// lights the media beyond far with it), into the 2D tail alone (a
			// layered attachment next to it would make the framebuffer incomplete)
			const GLenum buf = GL_COLOR_ATTACHMENT0;
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_2D, tr.froxelTailImage->texnum, 0);
			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
			qglDrawBuffers(1, &buf);
			GLSL_SetUniformInt(sp, UNIFORM_FROXELSLICE, -1);
			RB_InstantTriangle();
			qglDrawBuffers(numBufs, bufs);
		}
	}
	RB_VolumetricEndTimer(timer);

	// Front to back integration: one invocation per XY column, or one draw per slice.
	timer = RB_VolumetricBeginTimer("Froxel fog integrate");
	{
		shaderProgram_t *sp = compute ? &tr.volumetricIntegrateComputeShader : &tr.volumetricIntegrateShader;
		if ( !compute )
			FBO_Bind(tr.froxelIntegrateFbo);
		GL_SetViewportAndScissor(0, 0, s_vf.width, s_vf.height);
		GLSL_BindProgram(sp);
		GL_BindToTMU(tr.froxelInjectImage[current], TB_COLORMAP);
		GL_BindToTMU(tr.froxelDynamicImage, TB_NORMALMAP);
		if ( s_vf.rgb )
		{
			// T.rgb and its carry in draw buffers 2 and 3
			GL_BindToTMU(tr.froxelExtinctionImage[current], TB_FROXELEXTINCTION);
			if ( !compute )
				GL_SetScreenAuxWrite(true);
		}

		if ( compute )
		{
			bindOutput(0, tr.froxelIntegratedImage, GL_RGBA16F, true);
			bindOutput(1, tr.froxelTransmittanceImage, GL_RGBA16F, true);
			qglDispatchCompute((s_vf.width + 7) / 8, (s_vf.height + 7) / 8, 1);
			qglMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			for ( GLuint unit = 0; unit < 6; ++unit )
				bindOutput(unit, NULL, GL_RGBA16F, false);
		}
		else
		{
			for ( int k = 0; k < s_vf.depth; k++ )
			{
				const int carryWrite = k & 1;
				qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					tr.froxelIntegratedImage->texnum, 0, k);
				qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
					GL_TEXTURE_2D, tr.froxelCarryImage[carryWrite]->texnum, 0);
				GL_BindToTMU(tr.froxelCarryImage[carryWrite ^ 1], TB_LIGHTMAP);
				if ( s_vf.rgb )
				{
					qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
						tr.froxelTransmittanceImage->texnum, 0, k);
					qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
						GL_TEXTURE_2D, tr.froxelCarryTImage[carryWrite]->texnum, 0);
					GL_BindToTMU(tr.froxelCarryTImage[carryWrite ^ 1], TB_FROXELCARRYT);
				}

				GLSL_SetUniformInt(sp, UNIFORM_FROXELSLICE, k);
				RB_InstantTriangle();
			}
			if ( s_vf.rgb )
				GL_ResetScreenAuxWrite();
		}
	}
	RB_VolumetricEndTimer(timer);

	FBO_Bind(oldFbo);
}

qboolean RB_VolumetricCompositeActive( void )
{
	return (qboolean)(
		backEnd.volumetricView &&
		!backEnd.projection2D &&
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
	GLSL_BindProgram(sp);
	RB_VolumetricBindBlocks();
	GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
	GL_BindToTMU(tr.froxelIntegratedImage, TB_CUBEMAP);
	GL_BindToTMU(tr.froxelTailImage, TB_ENVBRDFMAP);
	if ( s_vf.rgb )
	{
		// RGB extinction: color * T.rgb (blend ZERO, SRC_COLOR), then + S
		// (ONE, ONE); the same for the glow. Two draws, in this order.
		GL_BindToTMU(tr.froxelTransmittanceImage, TB_FROXELTRANSMITTANCE);
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR);
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		GLSL_SetUniformInt(sp, UNIFORM_FROXELFOGMODE, 3);
		RB_InstantTriangle();
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		GLSL_SetUniformInt(sp, UNIFORM_FROXELFOGMODE, 4);
		RB_InstantTriangle();
	}
	else
	{
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_SRC_ALPHA);
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		RB_InstantTriangle();
	}
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
	// r_particleLightingDebug 1-4: the particle light field instead of a fog view
	const qboolean particleLightView = (qboolean)(tr.froxelParticleLightImage != NULL &&
		r_particleLightingDebug->integer >= 1 && r_particleLightingDebug->integer <= 4);
	if ( !s_vf.resources || (!r_volumetricFogDebug->integer && !particleLightView) || !s_vf.frameUsable )
		return;
	if ( backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE) )
		return;

	shaderProgram_t *sp = &tr.volumetricDebugShader;
	FBO_Bind(NULL);
	GL_SetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	GL_Cull(CT_TWO_SIDED);
	// views 18 (local fog volume bounds) and 28 (FX particle proxies) are drawn over the frame
	if ( !particleLightView && (r_volumetricFogDebug->integer == 18 || r_volumetricFogDebug->integer == 28) )
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
	if ( tr.froxelParticleLightImage )
		GL_BindToTMU(tr.froxelParticleLightImage, TB_ENTITYGRID_AMBIENT);
	// view 40: the extinction of this frame (r_volumetricSelfShadow)
	if ( tr.froxelMediaImage )
		GL_BindToTMU(tr.froxelMediaImage, TB_FROXELMEDIA);
	// views 51-56: RGB extinction (r_volumetricFogRGBExtinction)
	if ( s_vf.rgb )
	{
		GL_BindToTMU(tr.froxelTransmittanceImage, TB_FROXELTRANSMITTANCE);
		GL_BindToTMU(tr.froxelExtinctionImage[s_vf.current], TB_FROXELEXTINCTION);
	}
	// view 29: the dynamic light lists of the froxel slices
	if ( s_vfl.hasLights )
	{
		GL_BindToTMU(&s_vfl.lightImages[s_vfl.lightSlot], TB_FPLUS_LIGHTS);
		GL_BindToTMU(&s_vfl.listImages[s_vfl.lightSlot], TB_FPLUS_GRID);
	}
	{
		vec4_t particleLight = { particleLightView ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
		GLSL_SetUniformVec4(sp, UNIFORM_PARTICLELIGHT, particleLight);
	}
	RB_InstantTriangle();
}
