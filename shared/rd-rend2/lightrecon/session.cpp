// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "session.h"
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace Rend2LightRecon { namespace Core {

AssetBuilder::AssetBuilder(const InputManifest& manifest) : draft_(AssetMetadata(manifest)) {}
Error AssetBuilder::Finish(std::shared_ptr<const ReconstructedLightingAsset>& result) const
{
	result.reset();
	try
	{
		auto copy = std::make_shared<const ReconstructedLightingAsset>(draft_);
		std::string reason;
		if (!Validate(*copy, &reason)) return Failure(Code::InvalidInput, reason, "output");
		result = copy; return Error();
	}
	catch (const std::bad_alloc&) { return Failure(Code::MemoryLimit, "asset validation/copy allocation failed", "output"); }
}
Session::Session(InputDataset input, Options options, Services services, std::unique_ptr<Algorithm> algorithm)
	: input_(std::move(input), options.reads, services.cancellation), options_(options), services_(services),
	  builder_(input_.Metadata()), algorithm_(std::move(algorithm)) {}
bool Session::Cancelled() const { return services_.cancellation && services_.cancellation->IsCancelled(); }
Error Session::Fail(Error error)
{
	error_ = std::move(error); output_.reset();
	state_ = error_.code == Code::Cancelled ? SessionState::Cancelled :
		error_.code == Code::Unavailable ? SessionState::Unavailable : SessionState::Failed;
	if (services_.logger) services_.logger->Write(error_);
	return error_;
}
Error Session::Report(SessionState state)
{
	if (!services_.progress) return Error();
	Progress p; p.state = state; p.steps = steps_;
	Error error = services_.progress->Report(p);
	if (static_cast<unsigned>(error.code) > static_cast<unsigned>(Code::AlgorithmError))
		return Failure(Code::ProviderError, "progress sink returned an unknown status", "progress");
	return error;
}
Error Session::Begin()
{
	if (state_ != SessionState::Created) return Failure(Code::InvalidState, "session can begin only once");
	try
	{
		if (Cancelled()) return Fail(Failure(Code::Cancelled, "session cancelled before begin"));
		Error error = input_.ValidateMetadata(); if (!error) return Fail(error);
		if (!algorithm_) return Fail(Failure(Code::Unavailable, "no reconstruction algorithm is installed"));
		const Requirements needs = algorithm_->Needs();
		if (needs.geometry && !input_.Metadata().triangles) return Fail(Failure(Code::MissingGeometry, "algorithm requires geometry", "triangles"));
		if (needs.materials && !input_.Metadata().materials) return Fail(Failure(Code::Unavailable, "algorithm requires material data", "materials"));
		if (needs.visibility && !input_.HasVisibility()) return Fail(Failure(Code::Unavailable, "algorithm requires visibility provider", "visibility"));
		error = Report(SessionState::Running); if (!error) return Fail(error);
		if (Cancelled()) return Fail(Failure(Code::Cancelled, "session cancelled during begin"));
		state_ = SessionState::Running; return Error();
	}
	catch (const std::bad_alloc&) { return Fail(Failure(Code::MemoryLimit, "session setup allocation failed")); }
	catch (const std::exception& e) { return Fail(Failure(Code::AlgorithmError, e.what(), "begin")); }
	catch (...) { return Fail(Failure(Code::AlgorithmError, "session setup threw", "begin")); }
}
Error Session::Step()
{
	if (state_ != SessionState::Running) return Failure(Code::InvalidState, "session is not running");
	try
	{
		if (Cancelled()) return Fail(Failure(Code::Cancelled, "session cancelled"));
		if ((options_.maxSteps && steps_ >= options_.maxSteps) || steps_ == std::numeric_limits<uint64_t>::max())
			return Fail(Failure(Code::WorkLimit, "session reached its work-step limit"));
		AlgorithmContext context = { options_, services_, steps_++ };
		WorkResult work = algorithm_->Step(input_, builder_, context);
		if (Cancelled()) return Fail(Failure(Code::Cancelled, "session cancelled during work"));
		if (static_cast<unsigned>(work.error.code) > static_cast<unsigned>(Code::AlgorithmError))
			return Fail(Failure(Code::AlgorithmError, "algorithm returned an unknown error code"));
		if (!work.error) return Fail(work.error);
		if (work.next == Next::Unavailable) return Fail(Failure(Code::Unavailable, "algorithm cannot reconstruct this input"));
		if (work.next != Next::Continue && work.next != Next::Complete) return Fail(Failure(Code::AlgorithmError, "algorithm returned an invalid step state"));
		std::shared_ptr<const ReconstructedLightingAsset> candidate;
		if (work.next == Next::Complete)
		{
			Error error = builder_.Finish(candidate); if (!error) return Fail(error);
		}
		const SessionState next = work.next == Next::Complete ? SessionState::Completed : SessionState::Running;
		Error error = Report(next); if (!error) return Fail(error);
		if (Cancelled()) return Fail(Failure(Code::Cancelled, "session cancelled during progress report"));
		output_ = candidate; state_ = next; return Error();
	}
	catch (const std::bad_alloc&) { return Fail(Failure(Code::MemoryLimit, "algorithm allocation failed")); }
	catch (const std::exception& e) { return Fail(Failure(Code::AlgorithmError, e.what(), "step")); }
	catch (...) { return Fail(Failure(Code::AlgorithmError, "algorithm threw", "step")); }
}

} } // namespace Rend2LightRecon::Core
