// Producer-side reconstruction lifecycle. No estimator is installed by default.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "input.h"

namespace Rend2LightRecon { namespace Core {

enum class SessionState { Created, Running, Completed, Unavailable, Cancelled, Failed };
struct Options
{
	ReadLimits reads;
	uint64_t randomSeed = 0; // deterministic input to algorithms; core has no RNG/clock
	uint64_t maxSteps = 0; // zero = caller-controlled unlimited cooperative stepping
};
struct Progress
{
	SessionState state = SessionState::Created;
	uint64_t steps = 0;
};
class ProgressSink
{
public:
	virtual ~ProgressSink() = default;
	virtual Error Report(const Progress&) = 0; // explicit host failure/cancellation propagation
};
class Logger
{
public:
	virtual ~Logger() = default;
	virtual void Write(const Error&) noexcept = 0;
};
struct Services
{
	std::shared_ptr<const Cancellation> cancellation;
	std::shared_ptr<ProgressSink> progress;
	std::shared_ptr<Logger> logger;
};

class AssetBuilder
{
public:
	explicit AssetBuilder(const InputManifest& manifest);
	// Producer-only scratch metadata, not a published asset. Retaining a writable
	// reference cannot alter a finished result: Finish makes a validated deep copy.
	ReconstructedLightingAsset& Draft() { return draft_; }
	Error Finish(std::shared_ptr<const ReconstructedLightingAsset>& result) const;
private:
	ReconstructedLightingAsset draft_;
};
struct Requirements { bool geometry = false, materials = false, visibility = false; };
enum class Next { Continue, Complete, Unavailable };
struct WorkResult { Next next = Next::Unavailable; Error error; };
struct AlgorithmContext
{
	const Options& options;
	const Services& services;
	uint64_t step; // zero-based ordinal, no wall-clock scheduling inputs
};
class Algorithm
{
public:
	virtual ~Algorithm() = default;
	virtual Requirements Needs() const { return Requirements(); }
	// One algorithm instance per session, transferred to that session. Work must
	// be bounded/cooperative and poll cancellation during lengthy loops/provider I/O.
	// Context references are borrowed for this call only. No implicit input reads.
	virtual WorkResult Step(InputReader&, AssetBuilder&, const AlgorithmContext&) = 0;
};

class Session
{
public:
	Session(InputDataset input, Options options = Options(), Services services = Services(),
		std::unique_ptr<Algorithm> algorithm = std::unique_ptr<Algorithm>());
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;
	Error Begin(); // metadata/requirements only; no observation reads
	Error Step(); // explicitly advances one work unit; default core is Unavailable
	SessionState State() const { return state_; }
	uint64_t Steps() const { return steps_; }
	const Error& LastError() const { return error_; }
	std::shared_ptr<const ReconstructedLightingAsset> Output() const { return output_; }
private:
	Error Fail(Error error);
	Error Report(SessionState state);
	bool Cancelled() const;
	InputReader input_;
	Options options_;
	Services services_;
	AssetBuilder builder_;
	std::unique_ptr<Algorithm> algorithm_;
	SessionState state_ = SessionState::Created;
	uint64_t steps_ = 0;
	Error error_;
	std::shared_ptr<const ReconstructedLightingAsset> output_;
};

} } // namespace Rend2LightRecon::Core
