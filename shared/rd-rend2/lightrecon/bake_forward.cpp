// NetRadiant d9338309e direct-light subsets. No target observations are read.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bake_forward.h"
#include "bake_math.h"
#include "bake_trace.h"
#include <iomanip>
#include <locale>
#include <numeric>
#include <set>
#include <sstream>
namespace Rend2LightRecon
{
namespace Core
{
namespace Bake
{
using namespace Detail;
namespace
{
const char *Family = "NetRadiant-d9338309e-subsets-v2";
const char *Evidence = "docs/rend2-bake-model.md; tools/rend2/bake_model/source-lock.json; validation.json";
void Set(ProfileHypothesis &p, Feature f, const char *selector, Knowledge k = Knowledge::VerifiedSubset)
{
	auto &r = p.rules[size_t(f)];
	r.knowledge = k;
	r.selector = selector;
	r.provenance = Evidence;
	r.verificationScope = "Controlled planar super1 direct fixtures; not historical Raven";
}
void Param(ProfileHypothesis &p, Feature f, const char *name, double x)
{
	Parameter a;
	a.name = name;
	a.unit = "compiler-relative/explicit recipe";
	a.value.known = true;
	a.value.value = float(x);
	p.rules[size_t(f)].parameters.push_back(a);
}
double Get(const ProfileHypothesis &p, Feature f, const char *name)
{
	const auto *r = p.Rule(f);
	if (r)
		for (const auto &a : r->parameters)
			if (a.name == name && a.value.known)
				return a.value.value;
	return std::numeric_limits<double>::quiet_NaN();
}
Error ReadRecipe(const ProfileHypothesis &p, Recipe &r)
{
	if (p.schemaVersion != kProfileSchemaVersion || p.formulaVersion != kFormulaVersion)
		return Failure(Code::UnsupportedSchema, "Bake profile schema/formula");
	if (p.family != Family)
		return Failure(Code::Unavailable, "No implemented numerical law for this profile family");
	Options preflight;
	preflight.required = {Feature::Attenuation,
						  Feature::Angle,
						  Feature::PointScale,
						  Feature::Spot,
						  Feature::SurfaceIntegration,
						  Feature::SunSky,
						  Feature::GridReduction,
						  Feature::Deluxe,
						  Feature::OutputEncoding,
						  Feature::SamplingVisibility,
						  Feature::Ambient,
						  Feature::LightStyles,
						  Feature::ReceiverPlacement,
						  Feature::Filtering};
	Prediction checked;
	auto metadata = Require(p, preflight, checked);
	if (!metadata)
		return metadata;
	const auto a = p.Rule(Feature::Attenuation), n = p.Rule(Feature::Angle);
	if (!a || !n)
		return Failure(Code::Unavailable, "Missing attenuation/angle");
	if (a->selector == "isq")
		r.falloff = Falloff::InverseSquare;
	else if (a->selector == "linear")
		r.falloff = Falloff::Linear;
	else
		return Failure(Code::Unavailable, "Unknown attenuation selector");
	if (n->selector == "cosine")
		r.normal = NormalResponse::Cosine;
	else if (n->selector == "none")
		r.normal = NormalResponse::None;
	else if (n->selector == "half-lambert")
		r.normal = NormalResponse::HalfLambert;
	else
		return Failure(Code::Unavailable, "Unknown normal selector");
	struct Binding
	{
		Feature f;
		const char *name;
		double *value;
	};
	Binding b[] = {{Feature::PointScale, "scale", &r.photonsPerParameter},
				   {Feature::Attenuation, "minimum", &r.minimumDistanceWorld},
				   {Feature::Attenuation, "fade", &r.fade},
				   {Feature::Attenuation, "extra", &r.extraDistanceWorld},
				   {Feature::Angle, "angleScale", &r.angleScale},
				   {Feature::Attenuation, "tolerance", &r.tolerance},
				   {Feature::ReceiverPlacement, "push", &r.receiverPushWorld},
				   {Feature::SurfaceIntegration, "scale", &r.areaParameterScale},
				   {Feature::OutputEncoding, "gamma", &r.gamma},
				   {Feature::OutputEncoding, "compensate", &r.compensate},
				   {Feature::OutputEncoding, "exposure", &r.exposure}};
	for (auto &a : b)
	{
		*a.value = Get(p, a.f, a.name);
		if (!std::isfinite(*a.value))
			return Failure(Code::InvalidInput, "Missing/nonfinite recipe parameter", a.name);
	}
	double fast = Get(p, Feature::Attenuation, "fast"), srgb = Get(p, Feature::OutputEncoding, "srgb");
	if ((fast != 0 && fast != 1) || (srgb != 0 && srgb != 1))
		return Failure(Code::InvalidInput, "Recipe boolean");
	r.fast = fast != 0;
	r.srgbOutput = srgb != 0;
	if (Get(p, Feature::SunSky, "iterations") != 3)
		return Failure(Code::Unavailable, "Only explicit J=3 sky17 expansion is implemented");
	r.explicitSurfaceAmbient =
		V(Get(p, Feature::Ambient, "r"), Get(p, Feature::Ambient, "g"), Get(p, Feature::Ambient, "b"))
			.Float();
	if (r.photonsPerParameter <= 0 || r.minimumDistanceWorld <= 0 || r.fade <= 0 ||
		r.extraDistanceWorld < 0 || r.angleScale < 0 || r.tolerance <= 0 || r.receiverPushWorld < 0 ||
		r.areaParameterScale <= 0 || r.gamma <= 0 || r.compensate < 1 || r.exposure < 0 ||
		!Positive(r.explicitSurfaceAmbient))
		return Failure(Code::InvalidInput, "Invalid recipe range");
	const std::pair<Feature, const char *> selectors[] = {
		{Feature::PointScale, "photons-per-parameter"},
		{Feature::SamplingVisibility, "opaque-planar-conditional"},
		{Feature::Spot, "world-cone-16-32"},
		{Feature::SurfaceIntegration, "polygon-edge-integral"},
		{Feature::SunSky, "sun-and-sky17"},
		{Feature::GridReduction, "unpacked-single-pass"},
		{Feature::Deluxe, "direct-brightness-normal-bias"},
		{Feature::OutputEncoding, "shared-peak-ColorToBytes"},
		{Feature::ReceiverPlacement, "planar-normal-push"},
		{Feature::Filtering, "super1-unfiltered"},
		{Feature::LightStyles, "style0-only"},
		{Feature::Ambient, "explicit-surface-only"}};
	for (const auto &s : selectors)
	{
		const auto *f = p.Rule(s.first);
		if (!f || f->selector != s.second || f->knowledge == Knowledge::Unknown ||
			f->formulaVersion != kFormulaVersion)
			return Failure(Code::Unavailable, "Unimplemented profile selector", s.second);
	}
	return Error();
}
std::string Identity(const ProfileHypothesis &p)
{
	std::ostringstream k;
	k.imbue(std::locale::classic());
	k << std::setprecision(17) << p.family.size() << ':' << p.family << ' ' << p.schemaVersion << ' '
	  << p.formulaVersion << ' ' << p.id.size() << ':' << p.id << ' ' << int(p.applicability);
	k << ' ' << Disk::Hex(p.sourceLockKey) << ' ' << Disk::Hex(p.catalogueKey) << ' ' << int(p.compiler);
	k << ' ' << p.compilerSourceRevision << ' ' << p.compilerBinarySha256;
	for (const auto &r : p.rules)
	{
		k << ' ' << r.selector.size() << ':' << r.selector << ' ' << int(r.knowledge) << ' '
		  << r.formulaVersion << ' ' << r.provenance.size() << ':' << r.provenance;
		for (const auto &a : r.parameters)
			k << ' ' << a.name.size() << ':' << a.name << ' ' << a.value.known << ' ' << a.value.value;
	}
	return k.str();
}
V Sample(const Receiver &r, const Recipe &p, bool grid = false)
{
	return grid ? V(r.world) : V(r.world) + V(r.geometricNormal) * p.receiverPushWorld;
}
Error Amplitude(const SourceHypothesis &s, V &rgb)
{
	if (s.amplitudeDomain == AmplitudeDomain::CompilerRelative)
	{
		if (!s.amplitudeKnown || !Positive(s.compilerRgbAmplitude))
			return Failure(Code::InvalidInput, "Unknown/invalid compiler amplitude");
		rgb = V(s.compilerRgbAmplitude);
		return Error();
	}
	const auto &e = s.canonicalEmission;
	if (!e.intensity.known || !e.color.known || !s.compilerParametersPerCanonicalUnit.known ||
		!s.calibration.id || s.calibrationReference.empty() || s.calibrationMethod.empty())
		return Failure(Code::Unavailable, "Physical-to-compiler mapping is not specified");
	PhotometricUnit expected =
		s.kind == SourceKind::Directional
			? PhotometricUnit::Lux
			: s.kind == SourceKind::Polygon ? PhotometricUnit::Nits : PhotometricUnit::Candela;
	V c(e.color.rgb);
	double y = Dot(c, V(.2126, .7152, .0722));
	if (e.calibration != s.calibration.id || e.unit != expected || !Positive(c) || std::abs(y - 1) > 1e-4 ||
		!std::isfinite(e.intensity.value) || e.intensity.value < 0 ||
		!std::isfinite(s.compilerParametersPerCanonicalUnit.value) ||
		s.compilerParametersPerCanonicalUnit.value <= 0)
		return Failure(Code::InvalidInput, "Invalid explicit photometric mapping");
	rgb = c * (e.intensity.value * s.compilerParametersPerCanonicalUnit.value);
	return Error();
}
Error ValidateSource(const SourceHypothesis &s)
{
	if (!s.id || !std::isfinite(s.worldUnits.worldUnitsPerMeter) || s.worldUnits.worldUnitsPerMeter <= 0)
		return Failure(Code::InvalidInput, "Source ID/geometry units");
	if (s.position.sigmaWorld.known &&
		(!std::isfinite(s.position.sigmaWorld.value) || s.position.sigmaWorld.value < 0))
		return Failure(Code::InvalidInput, "Source position uncertainty");
	if (s.kind == SourceKind::Point || s.kind == SourceKind::Spot || s.kind == SourceKind::Polygon)
	{
		if (!s.position.known || !Finite(s.position.world))
			return Failure(Code::InvalidInput, "Source position");
	}
	if (s.geometry.known && (s.geometry.radiusWorld < 0 || s.geometry.halfWidthWorld < 0 ||
							 s.geometry.halfHeightWorld < 0 || s.geometry.halfLengthWorld < 0))
		return Failure(Code::InvalidInput, "Negative source dimension");
	if (s.geometry.known &&
		(!std::isfinite(s.geometry.radiusWorld) || !std::isfinite(s.geometry.halfWidthWorld) ||
		 !std::isfinite(s.geometry.halfHeightWorld) || !std::isfinite(s.geometry.halfLengthWorld)))
		return Failure(Code::InvalidInput, "Nonfinite source dimension");
	if (s.geometry.effectiveRadiusWorld.known &&
		(!std::isfinite(s.geometry.effectiveRadiusWorld.value) || s.geometry.effectiveRadiusWorld.value < 0))
		return Failure(Code::InvalidInput, "Invalid effective source size");
	if (s.geometry.effectiveRadiusWorld.known && s.geometry.effectiveRadiusWorld.value > 0)
		return Failure(Code::Unavailable, "Historical emitter expansion for finite radius is not specified");
	if (s.kind == SourceKind::Spot || s.kind == SourceKind::Directional || s.kind == SourceKind::Polygon)
	{
		if (!s.orientation.known || !Normal(s.orientation.forward))
			return Failure(Code::InvalidInput, "Source direction");
		if (!Normal(s.orientation.right) || !Normal(s.orientation.up) ||
			Length(Cross(V(s.orientation.right), V(s.orientation.up)) - V(s.orientation.forward)) > 1e-4 ||
			std::abs(Dot(V(s.orientation.right), V(s.orientation.up))) > 1e-4)
			return Failure(Code::InvalidInput, "Source frame is not right-handed orthonormal");
	}
	if (s.kind == SourceKind::Spot &&
		(!std::isfinite(s.spotTargetDistanceWorld) || s.spotTargetDistanceWorld <= 0 ||
		 !std::isfinite(s.spotRadiusWorld) || s.spotRadiusWorld < 0))
		return Failure(Code::InvalidInput, "Legacy spot target/radius");
	if (s.kind == SourceKind::Polygon)
	{
		if (!s.polygon.owner || !s.polygon.data || s.polygon.count < 3 || s.polygon.count > 4096)
			return Failure(Code::InvalidInput, "Expanded polygon lease/count");
		V a(s.polygon.data[0]), area;
		for (size_t i = 0; i < s.polygon.count; ++i)
		{
			V b(s.polygon.data[i]), c(s.polygon.data[(i + 1) % s.polygon.count]);
			if (!Finite(b) || std::abs(Dot(b - a, V(s.orientation.forward))) > 1e-3)
				return Failure(Code::InvalidInput, "Nonplanar source winding");
			area = area + Cross(b - a, c - a);
		}
		if (Length(area) < 1e-8)
			return Failure(Code::InvalidInput, "Degenerate area source");
		for (size_t i = 0; i < s.polygon.count; ++i)
			if (Dot(Cross(V(s.polygon.data[(i + 1) % s.polygon.count]) - V(s.polygon.data[i]),
						  V(s.polygon.data[(i + 2) % s.polygon.count]) -
							  V(s.polygon.data[(i + 1) % s.polygon.count])),
					area) < -1e-5)
				return Failure(Code::InvalidInput, "Nonconvex source winding");
	}
	V amplitude;
	return Amplitude(s, amplitude);
}
Error ValidateReceiver(const Receiver &r)
{
	if (!r.sample || !Finite(r.world) || !Normal(r.geometricNormal) || !Normal(r.shadingNormal) ||
		!std::isfinite(r.geometryErrorWorld) || r.geometryErrorWorld < 0 ||
		!std::isfinite(r.mappingErrorWorld) || r.mappingErrorWorld < 0)
		return Failure(Code::InvalidInput, "Invalid receiver ID/normal/position");
	return Error();
}
Disk::Digest FactorKey(const ProfileHypothesis &p, const Chunk<SourceHypothesis> &sources,
					   const Chunk<Receiver> &receivers, const EvaluationContext &c, const Options &o)
{
	std::ostringstream k;
	k.imbue(std::locale::classic());
	k << std::setprecision(17) << Identity(p) << ' ' << int(o.mode) << ' ' << int(o.trace) << ' '
	  << o.externallyCertifiedVisibility << ' ' << int(o.output) << ' ' << Disk::Hex(c.geometryKey)
	  << Disk::Hex(c.materialKey) << Disk::Hex(c.observationPolicyKey);
	if (c.compilerTrace)
		k << Disk::Hex(c.compilerTrace->Key());
	auto v = [&](Vec3 a) { k << ' ' << a.x << ' ' << a.y << ' ' << a.z; };
	for (size_t i = 0; i < sources.count; ++i)
	{
		const auto &s = sources.data[i];
		k << " source " << s.id << ' ' << int(s.kind) << ' ' << s.position.known << ' '
		  << s.orientation.known;
		v(s.position.world);
		v(s.orientation.forward);
		k << ' ' << s.spotTargetDistanceWorld << ' ' << s.spotRadiusWorld << ' ' << s.angular.twoSided << ' '
		  << s.worldUnits.worldUnitsPerMeter << ' ' << s.geometry.radiusWorld << ' '
		  << s.geometry.effectiveRadiusWorld.known << ' ' << s.geometry.effectiveRadiusWorld.value << ' '
		  << s.polygon.count;
		if (s.kind == SourceKind::Polygon)
			for (size_t j = 0; j < s.polygon.count; ++j)
				v(s.polygon.data[j]);
	}
	for (size_t i = 0; i < receivers.count; ++i)
	{
		const auto &r = receivers.data[i];
		k << " receiver " << r.sample << ' ' << r.surface << ' ' << r.material << ' ' << int(r.style) << ' '
		  << r.mappedPlanar << ' ' << r.compilerPlacementCertified << ' ' << r.twoSided << ' '
		  << r.geometryErrorWorld;
		v(r.world);
		v(r.geometricNormal);
		v(r.shadingNormal);
		k << ' ' << r.mappingErrorWorld;
	}
	std::string bytes = k.str();
	return Disk::Hash(bytes.data(), bytes.size());
}
Error Trace(const SourceHypothesis &s, const Receiver &r, V start, V incoming, const EvaluationContext &c,
			const Options &o, const ReadContext &read, Factor &factor, Work &work)
{
	if (o.externallyCertifiedVisibility)
	{
		factor.visibility = VisibilityState::Visible;
		factor.issues = PolicyAssumption;
		return Error();
	}
	if (++work.rays > o.maxRays)
		return Failure(Code::WorkLimit, "Forward ray budget");
	if (work.nodeTests + work.triangleTests >= o.maxWork)
		return Failure(Code::WorkLimit, "Forward traversal work budget");
	uint64_t remainingWork = o.maxWork - work.nodeTests - work.triangleTests;
	bool sky = s.kind == SourceKind::Directional || s.kind == SourceKind::Sky;
	V end = sky ? start + incoming * 65536 : V(s.position.world);
	RayResult hit;
	Error e;
	if (o.trace == TraceMode::CompilerTree)
	{
		if (!c.compilerTrace)
			return Failure(Code::MissingGeometry, "Missing compiler trace");
		e = c.compilerTrace->Query(start.Float(), end.Float(), sky, read, remainingWork, hit);
	}
	else
	{
		if (!c.visibility)
			return Failure(Code::MissingGeometry, "Missing static visibility provider");
		RayQuery q;
		q.startWorld = start.Float();
		q.endWorld = end.Float();
		q.purpose = sky ? RayPurpose::Sky : RayPurpose::Shadow;
		q.originSurface = r.surface;
		q.policy.requireKnownFreeOrigin = true;
		q.bias.absoluteWorld = 0;
		q.bias.relative = 0;
		q.maxWork = remainingWork;
		e = c.visibility->Query(q, read, hit);
	}
	if (!e)
		return e;
	factor.visibility = hit.state;
	factor.issues = hit.issues;
	work.triangleTests += hit.primitiveTests;
	work.nodeTests += hit.nodeTests;
	if (work.triangleTests + work.nodeTests > o.maxWork)
		return Failure(Code::WorkLimit, "Forward primitive budget");
	return Error();
}
double Polygon(const SourceHypothesis &s, V p, V n)
{
	V emitter(s.orientation.forward);
	double h = Dot(p - V(s.position.world), emitter);
	if (h < 3 && ((!s.angular.twoSided && h < -1) || (h > -3 && Dot(n, emitter) > .9)))
		return 0;
	if (h > -8 && h < 8)
		p = p + emitter * (8 - h);
	double total = 0;
	for (size_t i = 0; i < s.polygon.count; ++i)
	{
		V a = Unit(V(s.polygon.data[i]) - p), b = Unit(V(s.polygon.data[(i + 1) % s.polygon.count]) - p),
		  cross = Cross(a, b);
		if (Length(cross) < .0001)
			continue;
		total += Dot(n, Unit(cross)) * std::acos(Clamp(Dot(a, b), -1, 1));
		if (std::abs(total) > 6.3)
			return 0;
	}
	return std::abs(total * .159154942);
}
std::vector<V> Sky17()
{
	std::vector<V> v;
	double angle = 0;
	for (double elevation : {Pi / 12, Pi / 4})
	{
		for (int i = 0; i < 8; ++i)
		{
			v.push_back(V(std::cos(angle) * std::cos(elevation), std::sin(angle) * std::cos(elevation),
						  std::sin(elevation)));
			angle += Pi / 4;
		}
		angle += Pi / 8;
	}
	v.push_back(V(0, 0, 1));
	return v;
}
Error MakeFactor(const SourceHypothesis &s, const Receiver &r, const Recipe &p, const EvaluationContext &c,
				 const Options &o, const ReadContext &read, Factor &f, Work &w)
{
	f.source = s.id;
	f.observation = r.sample;
	V sample = Sample(r, p, o.output == Domain::GridAmbientDirected),
	  delta =
		  s.kind == SourceKind::Directional ? V(s.orientation.forward) * -1 : V(s.position.world) - sample;
	f.distanceWorld = Length(delta);
	f.incoming = Unit(delta).Float();
	const bool grid = o.output == Domain::GridAmbientDirected;
	V n(r.shadingNormal);
	double signedCos = Dot(n, V(f.incoming));
	f.cosine = grid ? 1 : r.twoSided ? std::abs(signedCos) : std::max(0.0, signedCos);
	if (s.kind == SourceKind::Point || s.kind == SourceKind::Spot)
	{
		if (f.distanceWorld < 1e-12)
			return Failure(Code::InvalidInput, "Coincident source/receiver direction");
		if (s.kind == SourceKind::Spot)
		{
			V d = sample - V(s.position.world), axis(s.orientation.forward);
			double axial = Dot(d, axis);
			double radius = (s.spotRadiusWorld + 16) / s.spotTargetDistanceWorld * axial;
			f.cone = axial < 0 ? 0 : Clamp((radius - Length(d - axis * axial)) / 32, 0, 1);
		}
	}
	if (s.kind == SourceKind::Polygon)
		f.polygon = Polygon(s, sample, n);
	if (!grid && !r.twoSided && signedCos <= 0)
	{
		f.visibility = VisibilityState::Visible;
		return Error();
	} // zero response needs no ray
	return Trace(s, r, sample, V(f.incoming), c, o, read, f, w);
}
V Value(const SourceHypothesis &s, const Factor &f, const Recipe &p, bool grid)
{
	V amplitude;
	Amplitude(s, amplitude);
	double I = Peak(amplitude);
	if (I <= 0 || f.visibility == VisibilityState::Blocked)
		return V();
	if (s.kind == SourceKind::Directional)
		return amplitude * f.cosine;
	if (s.kind == SourceKind::Polygon)
		return amplitude * (p.areaParameterScale * f.polygon);
	bool linear = p.falloff == Falloff::Linear && s.kind != SourceKind::Spot;
	double photons = I * p.photonsPerParameter;
	double envelope =
		linear ? (photons / 8000 - (p.fast ? p.tolerance : 0)) / p.fade
			   : p.fast ? std::sqrt(photons / p.tolerance) : std::numeric_limits<double>::infinity();
	if (grid ? f.distanceWorld > envelope : f.distanceWorld >= envelope)
		return V();
	double angle = f.cosine;
	if (!grid)
	{
		if (p.normal == NormalResponse::None && s.kind != SourceKind::Spot)
			angle = f.cosine > 0 ? 1 : 0;
		else if (p.normal == NormalResponse::HalfLambert)
			angle = angle > .001 ? std::pow(std::min(1.0, angle) * .5 + .5, 2) : 0;
		if (p.angleScale > 0)
			angle = std::min(1.0, angle / p.angleScale);
	}
	double d = std::max(p.minimumDistanceWorld, std::hypot(f.distanceWorld, p.extraDistanceWorld));
	double value = linear ? std::max(0.0, angle * photons / 8000 - d * p.fade) : photons * angle / (d * d);
	if (p.fast && value <= p.tolerance)
		return V();
	value *= f.cone;
	if (s.kind == SourceKind::Spot && p.fast && value <= p.tolerance)
		return V();
	return amplitude * (value / I);
}
Error Chunks(const Chunk<SourceHypothesis> &s, const Chunk<Receiver> &r, const ReadContext &read,
			 const Options &o)
{
	if (Cancelled(read))
		return Failure(Code::Cancelled, "Forward cancelled");
	if ((s.count && (!s.owner || !s.data)) || (r.count && (!r.owner || !r.data)))
		return Failure(Code::InvalidInput, "Forward chunk has no immutable lease");
	if (s.count && r.count > o.maxPairs / s.count)
		return Failure(Code::WorkLimit, "Forward pair budget");
	if (r.count > SIZE_MAX / sizeof(Prediction) ||
		(read.maxBytes && r.count > read.maxBytes / sizeof(Prediction)))
		return Failure(Code::MemoryLimit, "Forward result memory budget");
	if (s.count > 4096 || r.count > 1048576)
		return Failure(Code::WorkLimit, "Forward block count limit; split dataset into bounded batches");
	std::set<Id> ids;
	for (size_t i = 0; i < s.count; ++i)
	{
		if (!s.data[i].id || !ids.insert(s.data[i].id).second)
			return Failure(Code::InvalidInput, "Duplicate/zero source ID");
		if (o.mode != Mode::PhysicalIntegral)
		{
			auto e = ValidateSource(s.data[i]);
			if (!e)
				return e;
		}
	}
	ids.clear();
	for (size_t i = 0; i < r.count; ++i)
	{
		if (!ids.insert(r.data[i].sample).second)
			return Failure(Code::InvalidInput, "Duplicate observation ID");
		auto e = ValidateReceiver(r.data[i]);
		if (!e)
			return e;
	}
	return Error();
}
} // namespace
ProfileHypothesis NetRadiantProfile(const Recipe &r)
{
	ProfileHypothesis p;
	p.family = Family;
	p.compilerSourceRevision = "d9338309ea8fab75027b7382efaabe034302b862";
	p.compilerBinarySha256 = "894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4";
	p.id = std::string(Family) + (r.falloff == Falloff::Linear ? "/linear" : "/isq");
	p.compiler = CompilerIdentification::KnownControlledRecipe;
	p.applicability = Applicability::Unassessed;
	Set(p, Feature::Attenuation, r.falloff == Falloff::Linear ? "linear" : "isq");
	Set(p, Feature::Angle,
		r.normal == NormalResponse::None
			? "none"
			: r.normal == NormalResponse::HalfLambert ? "half-lambert" : "cosine");
	Set(p, Feature::PointScale, "photons-per-parameter");
	Set(p, Feature::Spot, "world-cone-16-32");
	Set(p, Feature::SurfaceIntegration, "polygon-edge-integral");
	Set(p, Feature::SunSky, "sun-and-sky17");
	Set(p, Feature::GridReduction, "unpacked-single-pass");
	Set(p, Feature::Deluxe, "direct-brightness-normal-bias");
	Set(p, Feature::OutputEncoding, "shared-peak-ColorToBytes");
	Set(p, Feature::Ambient, "explicit-surface-only");
	Set(p, Feature::LightStyles, "style0-only");
	Set(p, Feature::ReceiverPlacement, "planar-normal-push");
	Set(p, Feature::Filtering, "super1-unfiltered");
	Set(p, Feature::SamplingVisibility, "opaque-planar-conditional", Knowledge::SourceSupported);
	p.rules[size_t(Feature::BounceFill)].unsupported = {"Bounce transport replay is not implemented"};
	p.rules[size_t(Feature::Ambient)].unsupported = {"Flood, dirt, minlight and artist ambient inference"};
	p.rules[size_t(Feature::SunSky)].unsupported = {
		"Random sun jitter, arbitrary sky sample counts, Raven sky rules"};
	p.rules[size_t(Feature::Filtering)].unsupported = {
		"Receiver nudge in solids, supersampling, seam filtering, collapse"};
	p.rules[size_t(Feature::LightStyles)].unsupported = {"Animated styles and style overflow/reduction"};
	p.rules[size_t(Feature::GridReduction)].unsupported = {
		"Ordered compression, nudge, styles, bounce persistence"};
	Param(p, Feature::PointScale, "scale", r.photonsPerParameter);
	Param(p, Feature::Attenuation, "minimum", r.minimumDistanceWorld);
	Param(p, Feature::Attenuation, "fade", r.fade);
	Param(p, Feature::Attenuation, "extra", r.extraDistanceWorld);
	Param(p, Feature::Attenuation, "tolerance", r.tolerance);
	Param(p, Feature::Attenuation, "fast", r.fast ? 1 : 0);
	Param(p, Feature::Angle, "angleScale", r.angleScale);
	Param(p, Feature::ReceiverPlacement, "push", r.receiverPushWorld);
	Param(p, Feature::SurfaceIntegration, "scale", r.areaParameterScale);
	Param(p, Feature::OutputEncoding, "gamma", r.gamma);
	Param(p, Feature::OutputEncoding, "compensate", r.compensate);
	Param(p, Feature::OutputEncoding, "exposure", r.exposure);
	Param(p, Feature::OutputEncoding, "srgb", r.srgbOutput ? 1 : 0);
	Param(p, Feature::Ambient, "r", r.explicitSurfaceAmbient.x);
	Param(p, Feature::Ambient, "g", r.explicitSurfaceAmbient.y);
	Param(p, Feature::Ambient, "b", r.explicitSurfaceAmbient.z);
	Param(p, Feature::SunSky, "iterations", r.skyIterations);
	// LF-normalized catalogue/source-lock files: stable across Git autocrlf checkouts.
	const uint8_t sourceLock[32] = {0x56, 0xe3, 0x85, 0x06, 0x9a, 0x4d, 0xfb, 0xb5, 0x02, 0x44, 0xb7,
									0x13, 0x70, 0xc7, 0xa7, 0x5a, 0xda, 0xf5, 0xdd, 0x7b, 0x52, 0xed,
									0x41, 0xea, 0xd4, 0x1d, 0x09, 0xff, 0x25, 0x78, 0xb3, 0x14};
	std::copy(sourceLock, sourceLock + 32, p.sourceLockKey.begin());
	const uint8_t catalogue[32] = {0x86, 0x4f, 0x10, 0x3f, 0x6e, 0xe7, 0x60, 0xbf, 0x12, 0x4d, 0x16,
								   0x3e, 0xc7, 0xea, 0xdf, 0x82, 0x7d, 0x57, 0x73, 0xd3, 0x4b, 0x14,
								   0x61, 0x30, 0xe8, 0x9c, 0x55, 0xe3, 0x35, 0xac, 0xce, 0xe0};
	std::copy(catalogue, catalogue + 32, p.catalogueKey.begin());
	std::string identity = Identity(p);
	p.id += "/" + Disk::Hex(Disk::Hash(identity.data(), identity.size())).substr(0, 16);
	identity = Identity(p);
	p.contentKey = Disk::Hash(identity.data(), identity.size());
	return p;
}
Error ReceiverFromLuxel(Id id, const SurfaceLightingObservation &sample, const BakedSurface &source,
						bool placed, Receiver &out)
{
	out = Receiver();
	if (!id || sample.surface != source.ordinal || sample.materialIndex != source.materialIndex ||
		sample.styleSlot >= 4)
		return Failure(Code::InvalidInput, "Luxel/source identity mismatch");
	uint32_t excluded = LuxelOverlap | LuxelDegenerate | LuxelShadingMissing | LuxelBasisMismatch |
						LuxelMultipleCharts | LuxelUnsupported | LuxelNormalSeam | LuxelMixedFootprint |
						LuxelOrientationUnknown | LuxelPrecisionExceeded;
	if (sample.coverage != LuxelCoverage::Mapped || !sample.positionKnown || !sample.shadingNormalKnown ||
		!sample.normalOrientationKnown || (sample.issues & excluded) ||
		source.motion != SurfaceMotion::StaticWorld || source.excludedMaterial ||
		source.vertices.size() < 3 || source.materialFacts.cull == SurfaceCull::Unknown ||
		(source.kind != BakedSurfaceKind::Face && source.kind != BakedSurfaceKind::Triangles) ||
		source.lightmap[sample.styleSlot] < 0 ||
		uint32_t(source.lightmap[sample.styleSlot]) != sample.logicalLightmap)
		return Failure(Code::Unavailable, "Unsupported/ambiguous/moving luxel geometry");
	if (source.styles[sample.styleSlot] != 0)
		return Failure(Code::Unavailable, "Non-static light style");
	out.sample = id;
	out.surface = Id(source.ordinal) + 1;
	out.material = Id(source.materialIndex) + 1;
	out.world = sample.positionWorld;
	out.geometricNormal = sample.geometricNormal;
	out.shadingNormal = sample.shadingNormal;
	out.style = 0;
	out.mappedPlanar = true;
	out.compilerPlacementCertified = placed;
	out.mappingErrorWorld = sample.mappingErrorBoundWorld;
	out.twoSided = source.materialFacts.cull == SurfaceCull::None;
	for (const auto &v : source.vertices)
		if (std::abs(Dot(V(v.position) - V(sample.positionWorld), V(sample.geometricNormal))) > .002)
		{
			out = Receiver();
			return Failure(Code::Unavailable, "Nonplanar receiver chart");
		}
	return ValidateReceiver(out);
}
Support Capability(const ProfileHypothesis &p, SourceKind kind, const Receiver &r, const Options &o)
{
	Support s;
	if (o.mode != Mode::CompilerFaithful && o.mode != Mode::PhysicalIntegral &&
		o.mode != Mode::OptimizationSurrogate)
	{
		s.reason = "Unknown evaluation mode";
		return s;
	}
	if (o.trace != TraceMode::StaticGeometry && o.trace != TraceMode::CompilerTree)
	{
		s.reason = "Unknown trace implementation";
		return s;
	}
	if (kind != SourceKind::Point && kind != SourceKind::Spot && kind != SourceKind::Polygon &&
		kind != SourceKind::Directional && kind != SourceKind::Sky)
	{
		s.reason = "Unsupported historical source kind";
		return s;
	}
	if (o.output != Domain::EncodedRgb8BeforeQuantization && o.output != Domain::CompilerLinearRelative255 &&
		o.output != Domain::GridAmbientDirected && o.output != Domain::DeluxeWorldDirection)
	{
		s.reason = "Unimplemented output domain; no implicit conversion";
		return s;
	}
	s.operation = "super1/direct/surface";
	s.scope = "Known planar mapped receivers, explicit compiler amplitudes, pre-expanded sources";
	Recipe recipe;
	auto e = ReadRecipe(p, recipe);
	if (!e)
	{
		s.reason = e.message;
		return s;
	}
	Prediction preflight;
	e = Require(p, o, preflight);
	if (!e)
	{
		s.reason = e.message;
		return s;
	}
	if (o.mode == Mode::PhysicalIntegral)
	{
		s.reason = "Use separate PhysicalReference with authored SI photometry";
		return s;
	}
	for (Feature f : o.required)
		if (f == Feature::BounceFill || f == Feature::AbsoluteCalibration)
		{
			s.reason = "Metadata exists but requested numerical stage is not implemented";
			return s;
		}
	if (!o.allowAssumed && p.applicability != Applicability::VerifiedFeatureSubset)
	{
		s.reason = "Dataset/profile applicability is unverified; explicit allowAssumed required";
		return s;
	}
	if (kind == SourceKind::Tube || kind == SourceKind::Unsupported)
	{
		s.reason = "No native historical tube/unknown kernel; supply independently expanded polygons";
		return s;
	}
	if (r.style != 0 || !r.mappedPlanar || !r.compilerPlacementCertified || r.geometryErrorWorld > 0)
	{
		s.reason = "Unsupported style, receiver placement, patch, seam or mapping error";
		return s;
	}
	if (r.mappingErrorWorld > .002)
	{
		s.reason = "Numerical luxel mapping bound exceeds supported planar tolerance";
		return s;
	}
	if (o.output != Domain::GridAmbientDirected && Length(V(r.geometricNormal) - V(r.shadingNormal)) > 1e-5)
	{
		s.reason = "Compiler interpolation/push for distinct shading normals is not verified";
		return s;
	}
	if (o.output == Domain::GridAmbientDirected && !o.unpackedGridOnly)
	{
		s.reason = "Global ordered grid merging is not implemented";
		return s;
	}
	if (o.output == Domain::GridAmbientDirected && kind != SourceKind::Point && kind != SourceKind::Spot)
	{
		s.reason = "Unpacked grid operator supports point/spot contributions only";
		return s;
	}
	if (o.output == Domain::DeluxeWorldDirection &&
		(kind == SourceKind::Directional || kind == SourceKind::Sky || kind == SourceKind::Polygon))
	{
		s.reason = "Deluxe combination not numerically verified";
		return s;
	}
	s.implemented = true;
	s.fidelity = o.mode == Mode::OptimizationSurrogate ? Fidelity::Surrogate : Fidelity::ConditionalGeometry;
	s.reason = "General trace policy remains conditional, not complete compiler replay";
	return s;
}
Error Encode(const ProfileHypothesis &p, Vec3 input, Vec3 &pre, uint8_t packed[3])
{
	Recipe r;
	auto e = ReadRecipe(p, r);
	if (!e)
		return e;
	V v(input);
	if (!Positive(v))
		return Failure(Code::InvalidInput, "Encoding requires finite nonnegative accumulated RGB");
	for (int i = 0; i < 3; ++i)
		v[i] = 255 * std::pow(v[i] / 255, 1 / r.gamma);
	double peak = Peak(v), gain = r.exposure == 0
									  ? (peak > 255 ? 255 / peak : 1)
									  : (peak > 0 ? 255 * (-std::expm1(-peak / r.exposure)) / peak : 0);
	v = v * (gain / r.compensate);
	for (int i = 0; i < 3; ++i)
	{
		if (r.srgbOutput)
		{
			double x = v[i] / 255;
			v[i] = std::floor(255 * (x <= .0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - .055) + .5);
		}
		if (!std::isfinite(v[i]) || v[i] < 0 || v[i] > 255.00001)
			return Failure(Code::InvalidInput, "Undefined compiler byte conversion");
		v[i] = std::min(255.0, v[i]);
		packed[i] = uint8_t(v[i]);
	}
	pre = v.Float();
	return Error();
}
Error CpuForwardEvaluator::Prepare(const ProfileHypothesis &p, const Chunk<SourceHypothesis> &sources,
								   const Chunk<Receiver> &receivers, const EvaluationContext &c,
								   const Options &o, const ReadContext &read,
								   std::shared_ptr<const PreparedFactors> &out) const
{
	out.reset();
	auto e = Chunks(sources, receivers, read, o);
	if (!e)
		return e;
	Recipe recipe;
	e = ReadRecipe(p, recipe);
	if (!e)
		return e;
	if (sources.count && receivers.count > SIZE_MAX / sources.count)
		return Failure(Code::MemoryLimit, "Factor count overflow");
	size_t count = sources.count * receivers.count;
	if (count > SIZE_MAX / sizeof(Factor) || (read.maxBytes && count > read.maxBytes / sizeof(Factor)))
		return Failure(Code::MemoryLimit, "Optional factor block exceeds memory budget");
	if (o.trace == TraceMode::StaticGeometry && !o.externallyCertifiedVisibility &&
		c.geometryKey == Disk::Digest{})
		return Failure(Code::InvalidInput, "Cached visibility requires explicit geometry generation key");
	try
	{
		std::shared_ptr<PreparedFactors> f(new PreparedFactors);
		f->key_ = FactorKey(p, sources, receivers, c, o);
		f->records_.resize(count);
		Work w;
		for (size_t i = 0; i < receivers.count; ++i)
			for (size_t j = 0; j < sources.count; ++j)
			{
				if (Cancelled(read))
					return Failure(Code::Cancelled, "Prepare cancelled");
				if (sources.data[j].kind == SourceKind::Sky)
					return Failure(Code::Unavailable, "Sky expansion is not cached");
				if (!Capability(p, sources.data[j].kind, receivers.data[i], o).implemented)
					return Failure(Code::Unavailable, "Unsupported factor operation");
				e = MakeFactor(sources.data[j], receivers.data[i], recipe, c, o, read,
							   f->records_[i * sources.count + j], w);
				if (!e)
					return e;
			}
		out = f;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Factor allocation");
	}
}
Error CpuForwardEvaluator::Evaluate(const ProfileHypothesis &p, const Chunk<SourceHypothesis> &sources,
									const Chunk<Receiver> &receivers, const Decode::Profile &,
									const EvaluationContext &c, const Options &o, const ReadContext &read,
									Result &out) const
{
	out = Result();
	if (o.mode == Mode::PhysicalIntegral && o.factors)
		return Failure(Code::InvalidState,
					   "Historical factors cannot be substituted for a physical integral");
	auto e = Chunks(sources, receivers, read, o);
	if (!e)
		return e;
	if (o.factors && o.factors->Key() != FactorKey(p, sources, receivers, c, o))
		return Failure(Code::InvalidState, "Stale geometry/visibility factor key");
	try
	{
		auto predictions = std::make_shared<std::vector<Prediction>>(receivers.count);
		Result result;
		result.profileId = p.id;
		std::string identity = Identity(p);
		result.profileKey = Disk::Hash(identity.data(), identity.size());
		result.mode = o.mode;
		std::vector<size_t> order(sources.count);
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(),
				  [&](size_t a, size_t b) { return sources.data[a].id < sources.data[b].id; });
		Recipe recipe;
		Error recipeStatus = ReadRecipe(p, recipe);
		if (o.mode != Mode::PhysicalIntegral && !recipeStatus && recipeStatus.code != Code::Unavailable)
			return recipeStatus;
		for (size_t i = 0; i < receivers.count; ++i)
		{
			if (Cancelled(read))
				return Failure(Code::Cancelled, "Forward cancelled");
			const auto &r = receivers.data[i];
			auto &v = (*predictions)[i];
			v.observation = r.sample;
			v.domain = o.output;
			v.surrogate = o.mode == Mode::OptimizationSurrogate;
			if (o.mode == Mode::PhysicalIntegral)
			{
				Options remaining = o;
				if (result.work.quadrature + result.work.triangleTests >= o.maxWork)
					return Failure(Code::WorkLimit, "Physical batch work budget");
				remaining.maxWork = o.maxWork - result.work.quadrature - result.work.triangleTests;
				e = PhysicalReference(sources, r, c, remaining, read, v);
				if (!e)
					return e;
				result.work.quadrature += v.work.quadrature;
				result.work.triangleTests += v.work.triangleTests;
				continue;
			}
			Support support = Capability(p, sources.count ? sources.data[0].kind : SourceKind::Point, r, o);
			if (!recipeStatus || !support.implemented)
			{
				v.status =
					p.family == "UnknownProfile" ? Status::UnspecifiedModel : Status::UnsupportedFeature;
				v.reason = !recipeStatus ? recipeStatus.message : support.reason;
				continue;
			}
			if (o.trace == TraceMode::CompilerTree && c.compilerTrace &&
				c.compilerTrace->ClassifyOrigin(
					Sample(r, recipe, o.output == Domain::GridAmbientDirected).Float()) == PointDomain::Solid)
			{
				v.status = Status::UnsupportedFeature;
				v.reason = "Original mapped receiver is inside BSP solid; compiler nudge/placement replay "
						   "unavailable";
				v.visibilityIssues = OriginInsideSolid;
				continue;
			}
			v.fidelity = support.fidelity;
			v.issues = ConditionalTracePolicy | UnresolvedAbsolutePhotometricScale;
			if (p.applicability != Applicability::VerifiedFeatureSubset)
				v.issues |= AssumedProfileApplicability;
			if (o.mode == Mode::OptimizationSurrogate)
				v.issues |= ApproximationWithoutTotalErrorBound;
			Work before = result.work;
			V direct, sun, sky, dominant, deluxe;
			std::vector<std::pair<V, V>> grid;
			bool unknown = false, unsupported = false, scoped = true;
			V ambient(recipe.explicitSurfaceAmbient);
			for (size_t j : order)
			{
				const auto &s = sources.data[j];
				if ((s.position.sigmaWorld.known && s.position.sigmaWorld.value > 0) ||
					s.canonicalEmission.intensity.bounded)
					v.issues |= SourceParameterUncertainty;
				support = Capability(p, s.kind, r, o);
				if (!support.implemented)
				{
					unsupported = true;
					v.reason = support.reason;
					break;
				}
				std::vector<V> directions = s.kind == SourceKind::Sky ? Sky17() : std::vector<V>(1, V());
				V sourceRgb;
				for (V direction : directions)
				{
					if (Cancelled(read))
						return Failure(Code::Cancelled, "Forward pair evaluation cancelled");
					if (++result.work.pairs > o.maxPairs)
						return Failure(Code::WorkLimit, "Expanded source pair budget");
					++v.work.pairs;
					Factor f;
					if (s.kind == SourceKind::Sky)
					{
						f.source = s.id;
						f.observation = r.sample;
						f.incoming = direction.Float();
						f.cosine = o.output == Domain::GridAmbientDirected
									   ? 1
									   : std::max(0.0, Dot(V(r.shadingNormal), direction));
						e = Trace(s, r, Sample(r, recipe), direction, c, o, read, f, result.work);
					}
					else if (o.factors)
					{
						f = o.factors->records_[i * sources.count + j];
						++result.work.cacheHits;
						e = Error();
					}
					else
						e = MakeFactor(s, r, recipe, c, o, read, f, result.work);
					if (!e)
						return e;
					v.visibilityIssues |= f.issues;
					if (f.visibility == VisibilityState::Unknown)
					{
						unknown = true;
						continue;
					}
					V rgb;
					if (s.kind == SourceKind::Sky)
					{
						V amplitude;
						Amplitude(s, amplitude);
						rgb = f.visibility == VisibilityState::Visible ? amplitude * (f.cosine / 17) : V();
					}
					else
						rgb = Value(s, f, recipe, o.output == Domain::GridAmbientDirected);
					sourceRgb = sourceRgb + rgb;
					V incoming(f.incoming);
					grid.push_back(std::make_pair(rgb, incoming));
					dominant = dominant + incoming * Length(rgb);
					deluxe = deluxe + incoming * (Dot(rgb, V(.2989, .587, .114)) / 255);
				}
				if (s.kind == SourceKind::Directional)
					sun = sun + sourceRgb;
				else if (s.kind == SourceKind::Sky)
					sky = sky + sourceRgb;
				else
					direct = direct + sourceRgb;
				scoped &= s.kind == SourceKind::Directional && o.trace == TraceMode::CompilerTree &&
						  c.compilerTrace && c.compilerTrace->ScopedSunReplay() &&
						  !o.externallyCertifiedVisibility;
				if (o.contributions)
				{
					Contribution d;
					d.source = s.id;
					d.observation = r.sample;
					d.kind = s.kind;
					d.preencodingRgb = sourceRgb.Float();
					d.status = unknown ? Status::VisibilityUnknown : Status::AvailableConditional;
					d.globallyLinearInAmplitude = !recipe.fast && recipe.falloff == Falloff::InverseSquare;
					e = o.contributions->Write(d);
					if (!e)
						return e;
				}
			}
			v.work.rays = result.work.rays - before.rays;
			v.work.nodeTests = result.work.nodeTests - before.nodeTests;
			v.work.triangleTests = result.work.triangleTests - before.triangleTests;
			v.work.cacheHits = result.work.cacheHits - before.cacheHits;
			if (unsupported || unknown)
			{
				v.status = unsupported ? Status::UnsupportedFeature : Status::VisibilityUnknown;
				if (unknown)
					v.reason = "At least one potentially contributing visibility query is Unknown";
				continue;
			}
			// Verification scope is a numerical witness, not permission to label other
			// sun directions, receivers or source counts as verified compiler replay.
			if (scoped)
			{
				scoped = sources.count == 2 && o.output == Domain::EncodedRgb8BeforeQuantization &&
						 recipe.receiverPushWorld == 1 && recipe.gamma == 1 && recipe.compensate == 1 &&
						 recipe.exposure == 0 && !recipe.srgbOutput && Peak(ambient) == 0 &&
						 std::abs(r.world.z) < 1e-5 &&
						 std::max(std::abs(r.world.x), std::abs(r.world.y)) <= 272 &&
						 Length(V(r.shadingNormal) - V(0, 0, 1)) < 1e-6 && !r.twoSided;
				bool vertical = false, oblique = false;
				for (size_t j : order)
				{
					const auto &s = sources.data[j];
					vertical |= Length(V(s.compilerRgbAmplitude) - V(120, 120, 120)) < 1e-6 &&
								Length(V(s.orientation.forward) - V(0, 0, -1)) < 1e-6;
					oblique |= Length(V(s.compilerRgbAmplitude) - V(40, 0, 0)) < 1e-6 &&
							   Length(V(s.orientation.forward) - V(-std::sqrt(.5), 0, -std::sqrt(.5))) < 1e-6;
				}
				scoped &= vertical && oblique;
			}
			if (o.requireScopedCompilerReplay && !scoped)
			{
				v.status = Status::UnsupportedFeature;
				v.reason = "Requested complete scoped compiler replay is not supported for this combination";
				continue;
			}
			V total = direct + sun + sky + ambient;
			if (!Positive(total) || Peak(total) > std::numeric_limits<float>::max())
				return Failure(Code::InvalidInput, "Accumulation overflow");
			v.direct = direct.Float();
			v.sun = sun.Float();
			v.sky = sky.Float();
			v.ambient = ambient.Float();
			v.compilerRelative = total.Float();
			if (o.output == Domain::GridAmbientDirected)
			{
				if (c.orderedGridReduction)
				{
					v.status = Status::UnsupportedFeature;
					v.reason = "Ordered compressed grid not implemented";
					continue;
				}
				V axis = Unit(dominant), a, d;
				for (const auto &item : grid)
				{
					double q = std::max(0.0, Dot(item.second, axis));
					d = d + item.first * q;
					a = a + item.first * (.25 * (1 - q));
				}
				v.gridAmbient = a.Float();
				v.gridDirected = d.Float();
				v.directionKnown = Length(dominant) > 0;
				v.directionToSource = axis.Float();
				if (v.directionKnown)
				{
					v.encodedDirectionKnown = true;
					if (axis.x == 0 && axis.y == 0)
					{
						v.encodedDirection[0] = axis.z > 0 ? 0 : 128;
						v.encodedDirection[1] = 0;
					}
					else
					{
						v.encodedDirection[0] =
							uint8_t(int(std::acos(Clamp(axis.z, -1, 1)) * 255 / (2 * Pi)) & 255);
						v.encodedDirection[1] =
							uint8_t(int(std::atan2(axis.y, axis.x) * 255 / (2 * Pi)) & 255);
					}
				}
			}
			else if (o.output == Domain::DeluxeWorldDirection)
			{
				double bias = std::max(Dot(ambient, V(.2989, .587, .114)) * .9999 / 255, 1.0 / 256);
				V d = Unit(deluxe + V(r.shadingNormal) * bias);
				v.directionKnown = Length(d) > 0;
				v.directionToSource = d.Float();
				v.deluxeKnown = v.directionKnown;
				for (int k = 0; k < 3; ++k)
					v.deluxeRgb[k] = uint8_t(Clamp(127.5 + 127.5 * d[k], 0, 255));
			}
			else if (o.output == Domain::CompilerLinearRelative255)
				v.rgb = total.Float();
			else
			{
				e = Encode(p, total.Float(), v.encodedPrequantization, v.packedRgb);
				if (!e)
					return e;
				v.rgb = v.encodedPrequantization;
				v.packedKnown = true;
			}
			v.available = true;
			v.status = scoped ? Status::AvailableRelative : Status::AvailableConditional;
			if (scoped)
				v.fidelity = Fidelity::ScopedCompilerReplay;
			if (scoped)
				v.issues &= ~(ConditionalTracePolicy | AssumedProfileApplicability);
			if (o.mode == Mode::OptimizationSurrogate)
				v.fidelity = Fidelity::Surrogate;
			v.reason = scoped ? "Scoped compiler trace and planar super1 direct replay"
							  : "Conditional on explicit profile applicability, receiver placement and "
								"geometry trace policy";
		}
		result.predictions = VectorChunk<Prediction>(predictions, 0, predictions->size());
		out = std::move(result);
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Forward allocation");
	}
}
} // namespace Bake
} // namespace Core
} // namespace Rend2LightRecon
