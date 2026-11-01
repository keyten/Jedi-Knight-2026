// Typed provider failure classification. No inference from diagnostic strings.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_profile.h"
#include "input.h"
#include <cmath>
namespace Rend2LightRecon
{
namespace Core
{
inline Error ValidateProviderProfile(const Bake::ProfileHypothesis &p)
{
	if (p.schemaVersion != Bake::kProfileSchemaVersion || p.formulaVersion != Bake::kFormulaVersion)
		return Failure(Code::UnsupportedSchema, "Provider profile schema/formula mismatch");
	if (unsigned(p.compiler) >
			unsigned(Bake::CompilerIdentification::IndependentHistoricalProvenance) ||
		unsigned(p.applicability) > unsigned(Bake::Applicability::VerifiedFeatureSubset))
		return Failure(Code::InvalidInput, "Invalid provider profile classification");
	auto valid = [](const Bake::Parameter &x) {
		const auto &v = x.value;
		return std::isfinite(v.value) && std::isfinite(v.lower) && std::isfinite(v.upper) &&
			   (!v.bounded || v.lower <= v.upper);
	};
	for (const auto &r : p.rules)
	{
		if (unsigned(r.knowledge) > unsigned(Bake::Knowledge::VerifiedSubset))
			return Failure(Code::InvalidInput, "Invalid feature evidence status");
		if (r.formulaVersion != Bake::kFormulaVersion)
			return Failure(Code::UnsupportedSchema, "Provider feature formula mismatch");
		for (const auto &x : r.parameters)
			if (!valid(x))
				return Failure(Code::InvalidInput, "Invalid numerical provider profile parameter");
	}
	for (const auto &x : p.nuisanceBounds)
		if (!valid(x))
			return Failure(Code::InvalidInput, "Invalid provider nuisance bounds");
	return {};
}
inline ProviderOutcome ProviderFailure(const Error &e, ProviderOutcome declared,
									   ProviderBudgetScope &budget)
{
	switch (e.code)
	{
	case Code::Ok:
		return declared;
	case Code::Cancelled:
		return ProviderOutcome::Cancelled;
	case Code::InvalidInput:
	case Code::MissingGeometry:
		return ProviderOutcome::InvalidInput;
	case Code::ProviderError:
	case Code::InvalidState:
	case Code::UnsupportedSchema:
		return ProviderOutcome::Corrupt;
	case Code::AlgorithmError:
		return ProviderOutcome::NumericalFailure;
	case Code::MemoryLimit:
	case Code::WorkLimit:
		if (budget == ProviderBudgetScope::None)
			budget = ProviderBudgetScope::Global;
		return ProviderOutcome::BudgetExceeded;
	case Code::Unavailable:
		return declared == ProviderOutcome::NoSupportedEvidence
				   ? declared
				   : ProviderOutcome::UnsupportedCapability;
	}
	return ProviderOutcome::Corrupt;
}
} // namespace Core
} // namespace Rend2LightRecon
