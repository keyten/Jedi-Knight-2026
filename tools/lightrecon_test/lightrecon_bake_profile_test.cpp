// Contract preflight only; no mock lighting is production behavior.
#include "lightrecon/bake_profile.h"
#include <cstdio>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
int main()
{
	Bake::ProfileHypothesis p;
	Bake::Options options;
	options.required = {Bake::Feature::Attenuation};
	Bake::Prediction result;
	int failures = 0;
	if (result.directionKnown || result.encodedDirectionKnown || result.angularErrorRadians.known)
		++failures;
	if (Bake::Require(p, options, result).code != Code::Unavailable || result.available ||
		result.missing.size() != 1)
		++failures;
	auto &r = p.rules[static_cast<size_t>(Bake::Feature::Attenuation)];
	r.knowledge = Bake::Knowledge::VerifiedSubset;
	r.selector = "point_isq16";
	r.provenance = "controlled fixture";
	if (Bake::Require(p, options, result).code != Code::InvalidInput)
		++failures;
	r.verificationScope = "specific point fixtures only";
	r.formulaVersion = 999;
	if (Bake::Require(p, options, result).code != Code::UnsupportedSchema || result.available)
		++failures;
	r.formulaVersion = Bake::kFormulaVersion;
	if (!Bake::Require(p, options, result) || result.available)
		++failures; // preflight never evaluates
	options.requestedUnit = PhotometricUnit::Candela;
	if (Bake::Require(p, options, result).code != Code::Unavailable || result.available)
		++failures;
	p.formulaVersion = 999;
	if (Bake::Require(p, options, result).code != Code::UnsupportedSchema)
		++failures;
	std::printf("bake-profile contract: %d failures; no numerical evaluator/solver\n", failures);
	return failures ? 1 : 0;
}
