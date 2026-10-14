#include "tr_local.h"
#include "tr_pbr_contract.h"

#include <algorithm>
#include <vector>

float R_PBRMetersPerWorldUnit(void)
{
	return Rend2PBR::MetersPerWorldUnit(r_pbrWorldUnitsPerMeter->value);
}

float R_PBRPhysicalToScene(void)
{
	return Rend2PBR::FinitePositive(r_pbrPhysicalToScene->value,
		Rend2PBR::kDefaultPhysicalToScene);
}

void R_PBRCanonicalColor(const vec3_t rgb, float photometricY, vec3_t out)
{
	float chroma[3];
	if (!Rend2PBR::NormalizeChromaticity(rgb, chroma))
	{
		VectorClear(out);
		return;
	}
	const float canonicalY = Rend2PBR::PhysicalToScene(photometricY,
		R_PBRPhysicalToScene());
	VectorScale(chroma, canonicalY, out);
}

void R_PBRCalibrateLegacyMap(world_t *world)
{
	if (!world)
		return;
	world->pbrLegacyCalibration = 1.0f;
	world->pbrLegacyMedian = 0.0f;
	world->pbrLegacyP95 = 0.0f;
	if (!world->lightGridData || !world->lightGridArray ||
		world->numGridArrayElements <= 0)
		return;

	std::vector<float> samples;
	samples.reserve(world->numGridArrayElements);
	for (int i = 0; i < world->numGridArrayElements; ++i)
	{
		float rgb[3];
		if (world->hdrLightGrid)
		{
			const float *h = world->hdrLightGrid + i * 6;
			for (int c = 0; c < 3; ++c)
				rgb[c] = std::max(h[c] + h[c + 3], 0.0f);
		}
		else
		{
			const mgrid_t *g = world->lightGridData + world->lightGridArray[i];
			for (int c = 0; c < 3; ++c)
				rgb[c] = (g->ambientLight[0][c] + g->directLight[0][c]) / 255.0f;
		}
		const float y = Rend2PBR::Luminance(rgb);
		if (y > 1.0e-5f && std::isfinite(y))
			samples.push_back(y);
	}
	if (samples.empty())
		return;
	std::sort(samples.begin(), samples.end());
	const size_t middle = (samples.size() - 1) / 2;
	const size_t high = (samples.size() - 1) * 95 / 100;
	world->pbrLegacyMedian = samples[middle];
	world->pbrLegacyP95 = samples[high];
	world->pbrLegacyCalibration = Rend2PBR::LegacyMapCalibration(
		world->pbrLegacyMedian, world->pbrLegacyP95);
}

void R_PBRInfo_f(void)
{
	const float unitsPerMeter = r_pbrWorldUnitsPerMeter->value;
	const float physicalToScene = R_PBRPhysicalToScene();
	const float calibration = tr.world ? tr.world->pbrLegacyCalibration : 1.0f;
	ri.Printf(PRINT_ALL,
		"Rend2 PBR contract: mode %s\n"
		"  worldUnitsPerMeter %.6g (metresPerWorldUnit %.6g)\n"
		"  physicalToScene %.9g (sceneToPhysical %.9g)\n"
		"  baked legacy calibration %.6g (grid median %.6g, p95 %.6g)\n"
		"  dielectric F0 %.3f; roughness is perceptual, alpha = max(r^2, %.4g)\n"
		"  legacy roughness migration %s (r_pbrLegacyRoughness)\n",
		r_pbrPhysical->integer ? "physical" : "strict legacy",
		unitsPerMeter, R_PBRMetersPerWorldUnit(), physicalToScene,
		1.0f / physicalToScene, calibration,
		tr.world ? tr.world->pbrLegacyMedian : 0.0f,
		tr.world ? tr.world->pbrLegacyP95 : 0.0f,
		Rend2PBR::kDielectricF0, Rend2PBR::kMinGgxAlpha,
		r_pbrLegacyRoughness->integer ? "on" : "off");
	if (tr.refdef.num_dlights > 0)
	{
		const dlight_t *light = &tr.refdef.dlights[0];
		ri.Printf(PRINT_ALL,
			"  light[0]: range %.3f world units, canonical Y intensity %.6g scene (%s)\n",
			light->radius, light->canonicalIntensity,
			light->physicalIntensity ? "photometric source" : "legacy migration");
	}
	ri.Printf(PRINT_ALL,
		"  material view: r_autoPBRDebug 3; Forward+ light selection: r_forwardPlusDebug 9\n");
	if (ri.Cmd_Argc() > 1)
	{
		const float roughness = Com_Clamp(0.0f, 1.0f, (float)atof(ri.Cmd_Argv(1)));
		ri.Printf(PRINT_ALL, "  requested material: perceptual roughness %.6g, microfacet alpha %.6g\n",
			roughness, Rend2PBR::PerceptualRoughnessToAlpha(roughness));
	}
}
