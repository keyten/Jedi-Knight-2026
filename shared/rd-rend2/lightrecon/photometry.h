// Strict, pure CPU conversion of the canonical asset. No renderer consumers.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../tr_lightrecon_api.h"
#include "decode.h"

namespace Rend2LightRecon { namespace Photometric {
enum class Domain { CompilerRelativeQ, SourceLinearRelative, PhysicalSI, SceneEquivalent, SceneScaled };
enum class Code { Ok, Approximate, Unavailable, Incomparable, Invalid, Overflow };
enum class Reason { None, MissingValue, UnknownPrimaries, UnknownTransfer, UnknownAbsoluteScale,
	InvalidCalibration, ProfileDependent, AmbiguousConfiguration, IncompatibleQuantity,
	UnsupportedGeometry, UnsupportedAngularProfile, InvalidGeometry, InvalidNumber,
	DoubleSceneScaling, SceneScaledInput, UnsupportedLegacyAttenuation, NumericalOverflow };
template<class T> struct Conversion {
	Code code = Code::Unavailable;
	Reason reason = Reason::MissingValue;
	T value{};
	explicit operator bool() const { return code == Code::Ok || code == Code::Approximate; }
};
// Transient conversion receipt, NOT another persistent lighting model. Identity,
// evidence, calibration, root/path membership and uncertainty are never promoted.
struct Receipt {
	Source source;
	std::vector<SourceUncertainty> uncertainty;
	TransportMembership membership;
	bool fromField = false, fromContribution = false;
	TransportComponent fieldComponent = TransportComponent::IndirectIrradiance;
	ContributionComponent contributionComponent = ContributionComponent::Unresolved;
	std::vector<Id> sources;
	Id configuration = 0, sampleDomain = 0, dataset = 0;
	std::string domain, profile;
	bool rec709PrimariesAttested = false, linearTransferAttested = false;
	Id originalCalibration = 0;
	std::shared_ptr<const Core::Decode::Profile> observationProfile;
	uint32_t observationFlags = 0;
	uint8_t censoredChannels = 0;
};
struct Value {
	Vec3 rgb; // finite nonnegative linear Rec.709, or explicitly relative channels
	bool lowerKnown = false, upperKnown = false;
	Vec3 lower, upper; // componentwise feasible bounds, NOT confidence intervals
	PhotometricUnit unit = PhotometricUnit::Relative;
	PhotometricQuantity quantity = PhotometricQuantity::ReceiverIrradiance;
	Domain domain = Domain::SourceLinearRelative;
	Id calibration = 0;
	float appliedSceneScale = 0; // 0 canonical; nonzero only SceneScaled
	Receipt receipt;
};

Conversion<double> WorldDistanceMetres(double world, const Units&);
Conversion<double> MetresWorldDistance(double metres, const Units&);
// Strict decomposition rejects negative channels. Black has known Y=0 and
// unknown colour; near-black positive colour is normalized without a floor.
Conversion<Photometry> RGBToPhotometry(const Vec3&, PhotometricUnit, Id calibration);
// Only attested source-linear observations; never takes Decoder.scene/renderer
// values as canonical photometry. Caller supplies existing source identity.
Conversion<Value> DecodedRelative(const ReconstructedLightingAsset&, const Core::Decode::Observation&,
	PhotometricQuantity, Id calibration, const Receipt&);
Conversion<Value> Emission(const ReconstructedLightingAsset&, const PhysicalEmitter&);
Conversion<Value> EnvironmentRadiance(const ReconstructedLightingAsset&, const EnvironmentLighting&,
	const Vec3& directionalRGB); // supplied decoded directional sample, NOT mean->isotropic
Conversion<Value> Field(const ReconstructedLightingAsset&, const TransportField&, const Vec3&);
Conversion<Value> Contribution(const ReconstructedLightingAsset&, const ContributionReference&,
	Id source, const Vec3&); // rejects fractions/LegacyRelativeChannels
// For relative inputs the caller supplies the domain/profile/configuration and
// source attestation receipt. Unknown colour interpretation is never assumed.
Conversion<Value> CalibrateRelative(const ReconstructedLightingAsset&, const Value&);
Conversion<Value> RequirePhysical(const ReconstructedLightingAsset&, const Value&);
Conversion<Value> ToScene(const ReconstructedLightingAsset&, const Value&);
Conversion<Value> FromScene(const ReconstructedLightingAsset&, const Value&);

Conversion<double> SpotSolidAngle(const AngularDistribution&);
Conversion<double> SpotCandelaFromLumens(double lumens, const AngularDistribution&);
Conversion<double> EmittingAreaSquareMetres(const PhysicalEmitter&, const Units&);
enum class PointModel { IdealInverseSquare, FiniteRadiusBoundedApproximation };
struct Receiver { Vec3 positionWorld, normalWorld; double transmittance = 1; };
// Point/spot: E=I*f(theta)*max(n.wi,0)*V/d^2. Finite-radius option is
// explicitly approximate: max(d^2,r^2), never an exact finite surface integral.
Conversion<Value> PointReceiverIrradiance(const ReconstructedLightingAsset&, const PhysicalEmitter&,
	const Receiver&, PointModel, const Value* convertedEmission = nullptr);
// Rectangle/tube constant Lambertian emitted radiance. Direction is local
// outward at the sampled surface; it is not multiplied by cosine until integration.
Conversion<Value> LambertianEmittedRadiance(const ReconstructedLightingAsset&,
	const PhysicalEmitter&, const Vec3& surfaceNormal, const Vec3& directionOut, const Value* convertedEmission = nullptr);
// Deterministic midpoint area quadrature, explicitly approximate. No visibility
// tracing: Receiver.transmittance is a supplied uniform segment approximation.
Conversion<Value> AreaReceiverIrradiance(const ReconstructedLightingAsset&, const PhysicalEmitter&,
	const Receiver&, unsigned steps = 32, const Value* convertedEmission = nullptr);
// Parallel sun is a delta-direction measure of illuminance, NOT finite radiance.
Conversion<Value> DirectionalReceiverIrradiance(const ReconstructedLightingAsset&,
	const PhysicalEmitter&, const Receiver&, const Value* convertedEmission = nullptr);
// Explicit optional uniform apparent solar disk model: L=E_normal/(pi*sin^2(a)).
// No finite radiance exists for a zero-radius parallel delta source.
Conversion<Value> UniformSunDiskIncidentRadiance(const ReconstructedLightingAsset&,
	const PhysicalEmitter&, const Vec3& directionToSource, const Value* convertedEmission = nullptr);
struct IncidentSample { Value radiance; Vec3 directionToSource; double solidAngle = 0, transmittance = 1; };
// Same field/receipt for all samples; no accidental summation of alternative roots.
Conversion<Value> IntegrateIncidentRadiance(const ReconstructedLightingAsset&,
	const std::vector<IncidentSample>&, const Vec3& normalWorld);
Conversion<Value> UniformHemisphereIrradiance(const ReconstructedLightingAsset&, const Value& incidentRadiance);
// Quake radius attenuation has no exact inverse-square/source-shape equivalent.
Conversion<Value> LegacyAdapter(const Value&);
} } // namespace Rend2LightRecon::Photometric
