#include "tr_local.h"
#include "tr_pbr_contract.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

enum pbrTestLightType_t { PBR_TEST_POINT, PBR_TEST_SPOT, PBR_TEST_AREA };

struct pbrTestLight_t
{
	pbrTestLightType_t type;
	int unit;
	float value;
	float rangeMeters;
	float innerAngle;
	float outerAngle;
	float widthMeters;
	float heightMeters;
	qboolean placed;
	vec3_t origin;
	vec3_t dir;
	vec3_t right;
	vec3_t up;
};

std::vector<pbrTestLight_t> s_pbrTestLights;

const char *PBRTestTypeName(pbrTestLightType_t type)
{
	return type == PBR_TEST_POINT ? "point" : type == PBR_TEST_SPOT ? "spot" : "area";
}

void PBRTestUsage()
{
	ri.Printf(PRINT_ALL,
		"r_pbrLight point <cd|lm> <value> [range metres]\n"
		"r_pbrLight spot <cd|lm> <value> [range metres] [inner deg] [outer deg]\n"
		"r_pbrLight area <nits> [width metres] [height metres] [range metres]\n"
		"r_pbrLight list | clear\n"
		"Lights are placed two metres in front of the next world view.\n");
}

float PBRArg(int index, float fallback)
{
	return ri.Cmd_Argc() > index ? (float)atof(ri.Cmd_Argv(index)) : fallback;
}

} // namespace

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
		"  legacy roughness migration %s (r_pbrLegacyRoughness)\n"
		"  standard GGX multiple-scattering compensation %s (r_brdfEnergyCompensation)\n"
		"  standard diffuse/specular energy partition on; debug %d (r_pbrEnergyDebug)\n",
		r_pbrPhysical->integer ? "physical" : "strict legacy",
		unitsPerMeter, R_PBRMetersPerWorldUnit(), physicalToScene,
		1.0f / physicalToScene, calibration,
		tr.world ? tr.world->pbrLegacyMedian : 0.0f,
		tr.world ? tr.world->pbrLegacyP95 : 0.0f,
		Rend2PBR::kDielectricF0, Rend2PBR::kMinGgxAlpha,
		r_pbrLegacyRoughness->integer ? "on" : "off",
		r_brdfEnergyCompensation->integer ? "on" : "off",
		r_pbrEnergyDebug->integer);
	if (r_pbrPhysical->integer)
	{
		const float sunSceneY = Rend2PBR::Luminance(tr.refdef.sunCol) *
			(tr.world ? tr.world->pbrLegacyCalibration : 1.0f);
		const float sunLux = r_pbrSunIlluminance->value > 0.0f ?
			r_pbrSunIlluminance->value : Rend2PBR::SceneToPhysical(sunSceneY, physicalToScene);
		ri.Printf(PRINT_ALL, "  directional sun: %.3f lux (%s), canonical scene Y %.6g\n",
			sunLux, r_pbrSunIlluminance->value > 0.0f ? "authored" : "legacy-map equivalent",
			r_pbrSunIlluminance->value > 0.0f ?
				Rend2PBR::PhysicalToScene(sunLux, physicalToScene) : sunSceneY);
	}
	if (tr.refdef.num_dlights > 0)
	{
		int index = 0;
		if (ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "light"))
			index = Com_Clampi(0, tr.refdef.num_dlights - 1,
				ri.Cmd_Argc() > 2 ? atoi(ri.Cmd_Argv(2)) : 0);
		const dlight_t *light = &tr.refdef.dlights[index];
		const float distanceWorld = Distance(tr.refdef.vieworg, light->origin);
		const float distanceMeters = distanceWorld * R_PBRMetersPerWorldUnit();
		const float cullMeters = light->radius * R_PBRMetersPerWorldUnit();
		const float canonicalY = Rend2PBR::Luminance(light->color);
		const char *type = light->areaType == DLIGHT_RECT ? "rectangle" :
			light->areaType == DLIGHT_LINE ? "finite line/cylinder" :
			light->spot ? "spot" : "point";
		ri.Printf(PRINT_ALL,
			"  light[%d]: %s, %s source\n"
			"    cull radius %.3f world = %.3f m; viewer distance %.3f world = %.3f m\n"
			"    canonical scene Y %.6g\n",
			index, type, light->physicalIntensity ? "physical" : "legacy/migrated",
			light->radius, cullMeters, distanceWorld, distanceMeters, canonicalY);
		if (light->areaType == DLIGHT_POINT)
		{
			const float candela = Rend2PBR::SceneToPhysical(light->canonicalIntensity,
				physicalToScene);
			const float lux = Rend2PBR::SceneToPhysical(Rend2PBR::PointIrradiance(
				light->canonicalIntensity, distanceWorld, light->radius, unitsPerMeter,
				light->emitterRadius * R_PBRMetersPerWorldUnit()), physicalToScene) *
				(light->spot ? R_SpotConeAttenuation(light, tr.refdef.vieworg) : 1.0f);
			const float flux = light->spot ? candela * Rend2PBR::SpotProfileSolidAngle(
				light->spotCosInner, light->spotCosOuter) : candela * Rend2PBR::kFourPi;
			ri.Printf(PRINT_ALL,
				"    intensity %.3f cd (profile flux %.3f lm), emitter radius %.4f m, estimated viewer illuminance %.3f lux\n",
				candela, flux, light->emitterRadius * R_PBRMetersPerWorldUnit(), lux);
		}
		else
		{
			const float nits = Rend2PBR::SceneToPhysical(canonicalY, physicalToScene);
			const float widthMeters = 2.0f * light->halfWidth * R_PBRMetersPerWorldUnit();
			const float heightMeters = 2.0f * light->halfHeight * R_PBRMetersPerWorldUnit();
			ri.Printf(PRINT_ALL,
				"    %s luminance %.3f nits; dimensions %.3f x %.3f m\n",
				light->physicalIntensity ? "authored" : "canonical-equivalent",
				nits, widthMeters, heightMeters);
		}
	}
	ri.Printf(PRINT_ALL,
		"  material view: r_autoPBRDebug 3; Forward+ light selection: r_forwardPlusDebug 9\n");
	if (ri.Cmd_Argc() > 1 && Q_stricmp(ri.Cmd_Argv(1), "light"))
	{
		const float roughness = Com_Clamp(0.0f, 1.0f, (float)atof(ri.Cmd_Argv(1)));
		ri.Printf(PRINT_ALL, "  requested material: perceptual roughness %.6g, microfacet alpha %.6g\n",
			roughness, Rend2PBR::PerceptualRoughnessToAlpha(roughness));
	}
}

void R_PBRLight_f(void)
{
	if (ri.Cmd_Argc() < 2)
	{
		PBRTestUsage();
		return;
	}
	const char *kind = ri.Cmd_Argv(1);
	if (!Q_stricmp(kind, "clear"))
	{
		s_pbrTestLights.clear();
		ri.Printf(PRINT_ALL, "r_pbrLight: cleared\n");
		return;
	}
	if (!Q_stricmp(kind, "list"))
	{
		for (size_t i = 0; i < s_pbrTestLights.size(); ++i)
		{
			const pbrTestLight_t& l = s_pbrTestLights[i];
			ri.Printf(PRINT_ALL, "  %d: %s %.3f %s, range %.3f m%s\n", (int)i,
				PBRTestTypeName(l.type), l.value,
				l.type == PBR_TEST_AREA ? "nits" : l.unit == PHYSICALUNIT_LUMENS ? "lm" : "cd",
				l.rangeMeters, l.placed ? "" : " (awaiting world view)");
		}
		return;
	}
	if (!r_pbrPhysical->integer)
	{
		ri.Printf(PRINT_ALL, "r_pbrLight requires r_pbrPhysical 1 (vid_restart)\n");
		return;
	}
	if (s_pbrTestLights.size() >= 8)
	{
		ri.Printf(PRINT_ALL, "r_pbrLight: 8 lights at most; use clear\n");
		return;
	}
	pbrTestLight_t light = {};
	if (!Q_stricmp(kind, "area"))
	{
		light.type = PBR_TEST_AREA;
		light.value = Q_max(PBRArg(2, 2000.0f), 0.0f);
		light.widthMeters = Q_max(PBRArg(3, 1.0f), 0.01f);
		light.heightMeters = Q_max(PBRArg(4, 0.5f), 0.01f);
		light.rangeMeters = Q_max(PBRArg(5, 10.0f), 0.1f);
	}
	else if (!Q_stricmp(kind, "point") || !Q_stricmp(kind, "spot"))
	{
		light.type = !Q_stricmp(kind, "spot") ? PBR_TEST_SPOT : PBR_TEST_POINT;
		if (ri.Cmd_Argc() < 4 || (Q_stricmp(ri.Cmd_Argv(2), "cd") && Q_stricmp(ri.Cmd_Argv(2), "lm")))
		{
			PBRTestUsage();
			return;
		}
		light.unit = !Q_stricmp(ri.Cmd_Argv(2), "lm") ? PHYSICALUNIT_LUMENS : PHYSICALUNIT_CANDELA;
		light.value = Q_max(PBRArg(3, 1000.0f), 0.0f);
		light.rangeMeters = Q_max(PBRArg(4, 10.0f), 0.1f);
		light.innerAngle = Com_Clamp(0.0f, 89.0f, PBRArg(5, 20.0f));
		light.outerAngle = Com_Clamp(light.innerAngle, 89.0f, PBRArg(6, 30.0f));
	}
	else
	{
		PBRTestUsage();
		return;
	}
	s_pbrTestLights.push_back(light);
	ri.Printf(PRINT_ALL, "r_pbrLight: queued %s %.3f %s\n", PBRTestTypeName(light.type),
		light.value, light.type == PBR_TEST_AREA ? "nits" : light.unit == PHYSICALUNIT_LUMENS ? "lm" : "cd");
}

void R_PBRTestLightsBeginScene(const refdef_t *fd)
{
	if (!fd || (fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)))
		return;
	const float unitsPerMeter = r_pbrWorldUnitsPerMeter->value;
	for (pbrTestLight_t& l : s_pbrTestLights)
	{
		if (!l.placed)
		{
			VectorMA(fd->vieworg, 2.0f * unitsPerMeter, fd->viewaxis[0], l.origin);
			VectorCopy(fd->viewaxis[0], l.dir);
			VectorCopy(fd->viewaxis[1], l.right);
			VectorCopy(fd->viewaxis[2], l.up);
			l.placed = qtrue;
		}
		if (l.type == PBR_TEST_AREA)
		{
			vec3_t radiance;
			VectorSet(radiance, 1.0f, 1.0f, 1.0f);
			R_PBRCanonicalColor(radiance, l.value, radiance);
			R_AddPhysicalAreaLightToScene(l.origin, l.right, l.up,
				0.5f * l.widthMeters * unitsPerMeter,
				0.5f * l.heightMeters * unitsPerMeter,
				l.rangeMeters * unitsPerMeter,
				radiance[0], radiance[1], radiance[2], qtrue);
			continue;
		}
		refPhysicalLight_t light = {};
		light.type = l.type == PBR_TEST_SPOT ? PHYSICALLIGHT_SPOT : PHYSICALLIGHT_POINT;
		light.unit = l.unit;
		VectorCopy(l.origin, light.origin);
		VectorCopy(l.dir, light.dir);
		VectorSet(light.color, 1.0f, 1.0f, 1.0f);
		light.intensity = l.value;
		light.range = l.rangeMeters * unitsPerMeter;
		light.innerAngle = l.innerAngle;
		light.outerAngle = l.outerAngle;
		RE_AddPhysicalLightToScene(&light);
	}
}
