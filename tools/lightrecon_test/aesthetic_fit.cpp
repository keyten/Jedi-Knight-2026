// SPDX-License-Identifier: GPL-2.0-or-later
#include "aesthetic_fit.h"
#include "lightrecon/bake_math.h"
#include "lightrecon/geometry_key.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
namespace Rend2LightRecon
{
namespace Aesthetic
{
using namespace Core::Bake::Detail;
namespace
{
void Need(bool b, Code c, const char *s)
{
	if (!b)
		throw Failure(c, s);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
bool Nonnegative(Vec3 v)
{
	return Finite(v) && v.x >= 0 && v.y >= 0 && v.z >= 0;
}
double C(Vec3 v, unsigned c)
{
	return V(v)[c];
}
Vec3 RGB(const std::vector<double> &x, size_t j)
{
	return Vec3(float(x[j]), float(x[j + 1]), float(x[j + 2]));
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
int NormalBin(Vec3 n)
{
	Need(Normal(n), Code::Unavailable, "Missing receiver normal cannot define balance");
	int a = 0;
	for (int c = 1; c < 3; ++c)
		if (std::abs(C(n, c)) > std::abs(C(n, a)))
			a = c;
	return a * 2 + (C(n, a) < 0);
}
struct Rows
{
	std::vector<Shape::Observation> values;
	std::vector<double> weights;
	std::set<Id> ids, blocks;
	size_t normals = 0;
};
Rows Inspect(const std::vector<Shape::Observation> &rows, Shape::Role role, const Request &r)
{
	Need(!rows.empty() && rows.size() <= r.options.maxRows, Code::WorkLimit,
		 "Aesthetic partition row bounds");
	Rows out;
	out.values = rows;
	std::map<int, std::map<Id, double>> mass;
	for (const auto &a : rows)
	{
		Need(a.id && a.id == a.receiver.sample && a.spatialBlock && a.role == role &&
				 a.comparable && a.receiver.material == r.targets.material,
			 Code::Unavailable, "Role/material/comparability missing");
		Need(out.ids.insert(a.id).second, Code::InvalidInput, "Duplicate sample identity");
		Need(std::isfinite(a.area) && a.area > 0 && std::isfinite(a.sigma) && a.sigma > 0,
			 Code::InvalidInput, "Positive finite area/sigma required");
		for (const auto &v : a.channel)
			Need(v.present && std::isfinite(v.lower) && v.lower >= 0 && !std::isnan(v.upper) &&
					 v.upper >= v.lower && (std::isfinite(v.upper) || v.censored),
				 Code::Unavailable,
				 "All RGB intervals must be comparable; saturation must be censored");
		if (r.targets.domain == Domain::ForwardEncodedRgb8)
			for (const auto &v : a.channel)
				Need(v.lower <= 255 && (v.upper <= 255 || (v.censored && std::isinf(v.upper))),
					 Code::InvalidInput,
					 "Encoded targets must use RGB8 quantization/censoring intervals");
		mass[NormalBin(a.receiver.geometricNormal)][a.spatialBlock] += a.area;
		out.blocks.insert(a.spatialBlock);
	}
	out.normals = mass.size();
	for (const auto &a : rows)
	{
		const auto &group = mass.at(NormalBin(a.receiver.geometricNormal));
		double total = group.at(a.spatialBlock);
		Need(std::isfinite(total), Code::InvalidInput, "Receiver area overflow");
		out.weights.push_back(a.area / total / group.size() / mass.size() / 3);
	}
	return out;
}
void Disjoint(const Rows &a, const Rows &b)
{
	for (Id id : a.ids)
		Need(!b.ids.count(id), Code::InvalidInput, "Partition sample leakage");
	for (Id id : a.blocks)
		Need(!b.blocks.count(id), Code::InvalidInput, "Partition spatial block leakage");
}
struct Column
{
	bool available = false;
	std::vector<double> value;
	double rms = 0, discrepancy = 0;
	SourceSupport::Result placement;
	std::string reason;
};
using Columns = std::vector<std::vector<Column>>;
struct Meter
{
	uint64_t work = 0, rays = 0;
	const Options &options;
	const ReadContext &read;
	Meter(const Options &o, const ReadContext &r) : options(o), read(r) {}
	void Tick(uint64_t n = 1, uint64_t ray = 0)
	{
		Need(!(read.cancellation && read.cancellation->IsCancelled()), Code::Cancelled,
			 "Aesthetic fit cancelled");
		Need(n <= options.maxWork - work && ray <= options.maxRays - rays, Code::WorkLimit,
			 "Aesthetic cumulative work/ray budget");
		work += n;
		rays += ray;
	}
};
Columns Prepare(const Request &r, const Rows &rows, Meter &meter,
				const std::shared_ptr<const PointRefine::PlacementProvider> &placement)
{
	Columns out(r.fixtures.size());
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
	for (const auto &a : rows.values)
		receivers->push_back(a.receiver);
	for (size_t f = 0; f < r.fixtures.size(); ++f)
		for (const auto &e : r.fixtures[f].variants)
		{
			Column column;
			// Each alternative is a static hypothesis with its own admission. This is
			// not a certificate for physically sweeping a fixture between orientations.
			auto status = SourceSupport::Check(r.scene, *placement, e.geometry, e.geometry,
											   e.support, meter.read, column.placement,
											   SourceSupport::Admission::ModernFixedPoint);
			Check(status);
			meter.Tick(column.placement.work, column.placement.rays);
			if (!PointRefine::Admissible(column.placement.placement))
				column.reason = column.placement.reason;
			else
			{
				std::shared_ptr<const Shape::Block> block;
				auto sampling = r.sampling;
				sampling.maxWork = std::min(sampling.maxWork, r.options.maxWork - meter.work);
				sampling.maxRays = std::min(sampling.maxRays, r.options.maxRays - meter.rays);
				status = Shape::Prepare(r.model, {e}, Lease(receivers), r.geometry, sampling,
										meter.read, block);
				if (!status && status.code != Code::Unavailable)
					Check(status);
				if (!status)
					column.reason = status.message;
				else
				{
					const auto &prep = block->PreparationWork();
					meter.Tick(prep.traversal + prep.pairs + prep.nodes, prep.rays);
					std::vector<Shape::Prediction> p;
					Shape::Work work;
					Check(block->Evaluate({Vec3(1, 1, 1)}, meter.read, p, work));
					meter.Tick(work.traversal + work.pairs + work.values, work.rays);
					column.available = p.size() == rows.values.size();
					for (size_t i = 0; i < p.size(); ++i)
					{
						column.available &= p[i].available;
						double v = p[i].value.x * r.targets.gain;
						Need(std::isfinite(v) && v >= 0, Code::InvalidInput,
							 "Nonfinite modern basis");
						column.value.push_back(v);
						column.rms += rows.weights[i] * 3 * v * v;
						column.discrepancy = std::max(column.discrepancy,
													  p[i].quadratureDifference * r.targets.gain);
					}
					column.rms = std::sqrt(column.rms);
					Need(std::isfinite(column.rms), Code::InvalidInput,
						 "Relative gauge overflows basis diagnostics");
					if (!column.available)
						column.reason =
							"Unknown forward coverage; entire configuration unavailable";
				}
			}
			out[f].push_back(std::move(column));
		}
	return out;
}
double Residual(double p, const Amplitude::Interval &a)
{
	return p < a.lower ? p - a.lower : p > a.upper ? p - a.upper : 0;
}
double Huber(double e, double h)
{
	double v = std::abs(e);
	return v <= h ? .5 * e * e : h * (v - .5 * h);
}
struct State
{
	std::vector<size_t> variant;
	std::vector<double> x;
	bool converged = false;
	double loss = 0;
};
Score Evaluate(const Request &r, const Rows &rows, const Columns &columns,
			   const Configuration &config, const State &state, Meter &meter)
{
	Score score;
	score.rows = rows.values.size();
	score.blocks = rows.blocks.size();
	score.normalBins = rows.normals;
	for (size_t j = 0; j < config.fixtures.size(); ++j)
		if (!columns[config.fixtures[j]][state.variant[j]].available)
			return score;
	for (size_t i = 0; i < rows.values.size(); ++i)
	{
		meter.Tick(3 * (config.fixtures.size() + 1));
		V p;
		for (unsigned c = 0; c < 3; ++c)
		{
			p[c] = state.x[c] * r.targets.gain;
			for (size_t j = 0; j < config.fixtures.size(); ++j)
				p[c] +=
					columns[config.fixtures[j]][state.variant[j]].value[i] * state.x[3 + 3 * j + c];
		}
		Vec3 prediction = p.Float();
		if (r.targets.domain == Domain::ForwardEncodedRgb8)
		{
			uint8_t packed[3];
			Check(Bake::Encode(r.targets.encoding, prediction, prediction, packed));
		}
		Need(Finite(prediction), Code::InvalidInput, "Aesthetic prediction overflow");
		for (unsigned c = 0; c < 3; ++c)
		{
			double error = Residual(C(prediction, c), rows.values[i].channel[c]);
			score.mse += rows.weights[i] * error * error;
			score.robust += rows.weights[i] * Huber(error / rows.values[i].sigma, r.options.huber);
			score.maximumError = std::max(score.maximumError, std::abs(error));
		}
	}
	Need(std::isfinite(score.robust) && std::isfinite(score.mse), Code::InvalidInput,
		 "Aesthetic residual/sigma overflows objective");
	score.available = true;
	return score;
}
// Bounded derivative-free coordinate minimization also handles coupled forward
// encoding. A coarse scan precedes the local golden bracket; no linear RGB8 claim.
State Optimize(const Request &r, const Rows &rows, const Columns &columns,
			   const Configuration &config, State state, Meter &meter)
{
	std::vector<double> upper = {r.options.backgroundUpper.x, r.options.backgroundUpper.y,
								 r.options.backgroundUpper.z};
	for (size_t f : config.fixtures)
		for (unsigned c = 0; c < 3; ++c)
			upper.push_back(C(r.fixtures[f].upperPower, c));
	state.x.resize(upper.size(), 0);
	state.loss = Evaluate(r, rows, columns, config, state, meter).robust;
	for (size_t sweep = 0; sweep < r.options.maxSweeps; ++sweep)
	{
		double before = state.loss;
		for (size_t k = 0; k < upper.size(); ++k)
		{
			if (upper[k] == 0)
				continue;
			double best = state.loss, chosen = state.x[k], step = upper[k] / 8;
			auto probe = [&](double x) {
				state.x[k] = x;
				double value = Evaluate(r, rows, columns, config, state, meter).robust;
				if (value < best)
				{
					best = value;
					chosen = x;
				}
				return value;
			};
			for (unsigned b = 0; b <= 8; ++b)
				probe(b * step);
			double left = std::max(0., chosen - step), right = std::min(upper[k], chosen + step);
			const double ratio = .6180339887498948482;
			double a = right - ratio * (right - left), b = left + ratio * (right - left),
				   va = probe(a), vb = probe(b);
			for (unsigned it = 0; it < 32; ++it)
			{
				if (va < vb)
				{
					right = b;
					b = a;
					vb = va;
					a = right - ratio * (right - left);
					va = probe(a);
				}
				else
				{
					left = a;
					a = b;
					va = vb;
					b = left + ratio * (right - left);
					vb = probe(b);
				}
			}
			state.x[k] = chosen;
			state.loss = best;
		}
		if (before - state.loss <= r.options.tolerance * (1 + before))
		{
			state.converged = true;
			break;
		}
	}
	return state;
}
void Validate(const Request &r, const ReadContext &read, bool sparse = false)
{
	Need(r.model.law == Shape::Law::DiffuseIntegral &&
			 r.model.output == Shape::Output::RelativeIncident,
		 Code::Unavailable,
		 "Aesthetic objective requires modern relative transport; historical solver unchanged");
	Need((r.targets.domain == Domain::RelativeLinearAttested ||
		  r.targets.domain == Domain::ForwardEncodedRgb8) &&
			 (r.targets.comparableMaterialResponseAttested ||
			  (r.targets.domain == Domain::ForwardEncodedRgb8 &&
			   r.targets.sameMaterialResponseAssumed)) &&
			 r.targets.material && !r.targets.provenance.empty(),
		 Code::Unavailable,
		 "Explicit target domain and comparable material response required; unknown albedo is not "
		 "lamp energy");
	Need(std::isfinite(r.targets.gain) && r.targets.gain > 0, Code::InvalidInput,
		 "Positive fixed relative gauge required");
	if (r.targets.domain == Domain::ForwardEncodedRgb8)
	{
		Vec3 p;
		uint8_t b[3];
		Check(Bake::Encode(r.targets.encoding, Vec3(1, 1, 1), p, b));
	}
	const auto &o = r.options;
	Need(o.maxRows > 0 && o.maxRows <= 4096 && o.maxSweeps > 0 && o.maxSweeps <= 200 &&
			 o.maxGeometrySweeps <= 8 && std::isfinite(o.huber) && o.huber > 0 &&
			 std::isfinite(o.tolerance) && o.tolerance > 0 &&
			 std::isfinite(o.directionImprovement) && o.directionImprovement > 0 &&
			 std::isfinite(o.validationPenaltyPerFixture) && o.validationPenaltyPerFixture >= 0 &&
			 std::isfinite(o.validationEquivalentSlack) && o.validationEquivalentSlack >= 0 &&
			 Nonnegative(o.backgroundUpper),
		 Code::InvalidInput, "Invalid bounded aesthetic objective options");
	Need(r.fixtures.size() <= (sparse ? 128u : 16u) &&
			 (sparse ? r.configurations.empty() :
			  (!r.configurations.empty() && r.configurations.size() <= 32)),
		 Code::WorkLimit, "Aesthetic configuration bounds");
	size_t variants = 0;
	std::set<Id> ids;
	for (const auto &f : r.fixtures)
	{
		Need(!f.variants.empty() && f.variants.size() <= 32 && !f.provenance.empty() &&
				 Nonnegative(f.upperPower),
			 Code::InvalidInput, "Fixture prior/power bounds required");
		variants += f.variants.size();
		const auto &base = f.variants[0];
		Need(ids.insert(base.geometry.source.id).second, Code::InvalidInput,
			 "Duplicate fixture identity");
		Need(base.support.kind != SourceSupport::Kind::LegacyVacuum, Code::Unavailable,
			 "Explicit supported fixture contract required");
		for (const auto &e : f.variants)
		{
			Need(e.geometry.source.state == HypothesisState::Candidate &&
					 e.geometry.source.id == base.geometry.source.id &&
					 e.geometry.kind == base.geometry.kind &&
					 Length(V(e.geometry.position.world) - V(base.geometry.position.world)) == 0 &&
					 SourceSupport::Key(e.support) == SourceSupport::Key(base.support),
				 Code::InvalidInput, "Fixture position/identity/support must remain fixed");
			const auto &a = e.geometry.geometry;
			const auto &b = base.geometry.geometry;
			Need(a.known == b.known && a.halfWidthWorld == b.halfWidthWorld &&
					 a.halfHeightWorld == b.halfHeightWorld &&
					 a.halfLengthWorld == b.halfLengthWorld && a.radiusWorld == b.radiusWorld &&
					 a.effectiveRadiusWorld.known == b.effectiveRadiusWorld.known &&
					 a.effectiveRadiusWorld.value == b.effectiveRadiusWorld.value &&
					 e.emittingSurface == base.emittingSurface &&
					 e.geometry.angular.twoSided == base.geometry.angular.twoSided,
				 Code::InvalidInput,
				 "Only bounded spot axis/cone can vary; supported dimensions are fixed");
			if (f.variants.size() > 1)
			{
				Need(base.geometry.kind == EmitterKind::Spot && !f.directionProvenance.empty() &&
						 std::isfinite(f.maximumAxisRadians) && f.maximumAxisRadians >= 0 &&
						 f.maximumAxisRadians <= .8 && std::isfinite(f.minimumOuterRadians) &&
						 std::isfinite(f.maximumOuterRadians) && f.minimumOuterRadians > 0 &&
						 f.maximumOuterRadians < 1.57079632679 &&
						 f.minimumOuterRadians <= f.maximumOuterRadians,
					 Code::Unavailable,
					 "Only independently constrained bounded spot alternatives supported");
				double axis = std::acos(
					Clamp(Dot(V(base.geometry.frame.forward), V(e.geometry.frame.forward)), -1, 1));
				double cone = std::acos(Clamp(e.geometry.angular.cosOuter, -1, 1));
				Need(axis <= f.maximumAxisRadians + 1e-6 && cone >= f.minimumOuterRadians - 1e-6 &&
						 cone <= f.maximumOuterRadians + 1e-6,
					 Code::InvalidInput, "Spot alternative exceeds independent axis/cone bounds");
			}
		}
	}
	Need(r.sampling.maxBytes <= 256ull * 1024 * 1024 && o.maxBytes <= 1024ull * 1024 * 1024,
		 Code::MemoryLimit, "Aesthetic bounded scratch/memory policy");
	uint64_t bytes = 4ull * 1024 * 1024 + r.sampling.maxBytes +
					 uint64_t(o.maxRows) * (variants * 24 + sizeof(Shape::Observation) * 3 + 128);
	Need(bytes <= o.maxBytes && (!read.maxBytes || bytes <= read.maxBytes), Code::MemoryLimit,
		 "Aesthetic snapshot/basis memory preflight");
	for (const auto &config : r.configurations)
	{
		Need(!config.name.empty(), Code::InvalidInput, "Named configuration required");
		std::set<size_t> seen;
		for (size_t f : config.fixtures)
			Need(f < r.fixtures.size() && seen.insert(f).second, Code::InvalidInput,
				 "Invalid configuration fixture index");
	}
	Need(r.scene.geometry && r.geometry.visibility &&
			 r.scene.geometry->CertificationKey() == r.geometry.geometryKey &&
			 r.geometry.visibility.get() == r.scene.geometry.get(),
		 Code::MissingGeometry, "Matching immutable placement/visibility required");
}
Fit MakeFit(const Request &r, const Rows &rows, const Columns &columns, const Configuration &config,
			State &state, Meter &meter)
{
	Fit fit;
	fit.name = config.name;
	for (size_t f : config.fixtures)
		if (!columns[f][0].available)
		{
			fit.limitation = columns[f][0].reason;
			return fit;
		}
	state.variant.resize(config.fixtures.size(), 0);
	state = Optimize(r, rows, columns, config, state, meter);
	std::vector<double> directionGain(config.fixtures.size(), 0);
	if (rows.blocks.size() >= 3)
		for (size_t sweep = 0; sweep < r.options.maxGeometrySweeps; ++sweep)
		{
			bool changed = false;
			for (size_t j = 0; j < config.fixtures.size(); ++j)
			{
				size_t f = config.fixtures[j];
				State best = state;
				for (size_t v = 0; v < columns[f].size(); ++v)
					if (v != state.variant[j] && columns[f][v].available)
					{
						State trial = state;
						trial.variant[j] = v;
						trial.converged = false;
						trial = Optimize(r, rows, columns, config, trial, meter);
						if (trial.loss + r.options.directionImprovement < best.loss)
							best = std::move(trial);
					}
				if (best.loss + r.options.directionImprovement < state.loss)
				{
					directionGain[j] += state.loss - best.loss;
					state = std::move(best);
					changed = true;
				}
			}
			if (!changed)
				break;
		}
	fit.available = true;
	fit.converged = state.converged;
	fit.background = RGB(state.x, 0);
	for (unsigned c = 0; c < 3; ++c)
		fit.backgroundAtBound |=
			C(r.options.backgroundUpper, c) > 0 &&
			C(fit.background, c) >= C(r.options.backgroundUpper, c) * (1 - 1e-5);
	fit.training = Evaluate(r, rows, columns, config, state, meter);
	for (size_t j = 0; j < config.fixtures.size(); ++j)
	{
		size_t f = config.fixtures[j], v = state.variant[j];
		const auto &column = columns[f][v];
		SourceDiagnostic d;
		d.fixture = r.fixtures[f].variants[v].geometry.source.id;
		d.variant = v;
		d.emitter = r.fixtures[f].variants[v];
		d.relativePower = RGB(state.x, 3 + j * 3);
		d.emitter.rgbAmplitude = d.relativePower;
		d.columnRms = column.rms;
		d.placement = column.placement;
		d.directionTrainingGain = directionGain[j];
		d.directionConstrained = v != 0 && directionGain[j] > r.options.directionImprovement;
		d.quadratureDifference = column.discrepancy;
		for (unsigned c = 0; c < 3; ++c)
			d.atPowerBound |= C(r.fixtures[f].upperPower, c) > 0 &&
							  C(d.relativePower, c) >= C(r.fixtures[f].upperPower, c) * (1 - 1e-5);
		for (size_t k = 0; k < config.fixtures.size(); ++k)
			if (j != k)
			{
				const auto &other = columns[config.fixtures[k]][state.variant[k]];
				double dot = 0;
				for (size_t i = 0; i < rows.values.size(); ++i)
					dot += rows.weights[i] * 3 * column.value[i] * other.value[i];
				if (column.rms > 0 && other.rms > 0)
					d.maximumColumnCorrelation =
						std::max(d.maximumColumnCorrelation, dot / column.rms / other.rms);
			}
		fit.sources.push_back(d);
	}
	fit.limitation = "Conditional aesthetic hypothesis; arbitrary relative scale; bounded "
					 "background is not recovered bounce; source truth unavailable";
	if (r.targets.sameMaterialResponseAssumed)
		fit.limitation +=
			"; same-material response assumed for encoded appearance, not calibrated irradiance";
	return fit;
}
} // namespace
struct Experiment::Data
{
	Request request;
	Rows training, validation;
	std::vector<Fit> fits;
	Fit baseline;
	std::vector<State> states;
	std::vector<size_t> alternatives;
	State baselineState;
	SparseReport sparse;
	std::vector<FixtureEvidence> evidence;
	size_t selected = SIZE_MAX;
	uint64_t work = 0;
	std::shared_ptr<const PointRefine::PlacementProvider> placement;
};
const std::vector<Fit> &Experiment::Fits() const
{
	static const std::vector<Fit> empty;
	return data ? data->fits : empty;
}
const Fit &Experiment::Baseline() const
{
	static const Fit empty;
	return data ? data->baseline : empty;
}
size_t Experiment::Selected() const
{
	return data ? data->selected : SIZE_MAX;
}
const std::vector<size_t> &Experiment::ValidationAlternatives() const
{
	static const std::vector<size_t> empty;
	return data ? data->alternatives : empty;
}
uint64_t Experiment::Work() const
{
	return data ? data->work : 0;
}
const SparseReport &Experiment::Sparse() const
{
	static const SparseReport empty;
	return data ? data->sparse : empty;
}
#include "aesthetic_sparse.inc"
namespace
{
Error RunExperiment(const Request &input, const SparsePolicy *policy,
					const std::vector<Shape::Observation> &train,
					const std::vector<Shape::Observation> &valid, const ReadContext &read,
					std::shared_ptr<const Experiment::Data> &out)
{
	out.reset();
	try
	{
		auto d = std::make_shared<Experiment::Data>();
		if (policy)
			NormalizeSparse(input, *policy, d->request, d->sparse, d->evidence);
		else
		{
			Validate(input, read);
			d->request = input;
		}
		auto &r = d->request;
		if (policy)
		{
			Validate(r, read, true);
			size_t variants = 0;
			for (const auto &f : r.fixtures)
				variants += f.variants.size();
			uint64_t bytes = 4ull * 1024 * 1024 + r.sampling.maxBytes +
				uint64_t(r.options.maxRows) * (variants * 24 + sizeof(Shape::Observation) * 3 + 128) +
				uint64_t(policy->maxModels) * (sizeof(Fit) + sizeof(State) + 256 +
				std::min(policy->maxSources, r.fixtures.size()) * (sizeof(SourceDiagnostic) + 256));
			Need(bytes <= r.options.maxBytes && (!read.maxBytes || bytes <= read.maxBytes),
				Code::MemoryLimit, "Sparse catalog/basis memory preflight");
		}
		Meter meter(r.options, read);
		meter.Tick();
		d->training = Inspect(train, Shape::Role::Training, r);
		d->validation = Inspect(valid, Shape::Role::Validation, r);
		Disjoint(d->training, d->validation);
		Check(PointRefine::MakePlacement(r.scene, read, d->placement));
		r.geometry.visibility = PointRefine::WithOccupancy(r.geometry.visibility, d->placement);
		auto columns = Prepare(r, d->training, meter, d->placement);
		Configuration background;
		background.name = "no-local-light";
		d->baseline = MakeFit(r, d->training, columns, background, d->baselineState, meter);
		if (policy)
			SparseTraining(*d, columns, meter);
		else
		{
			d->states.resize(r.configurations.size());
			for (size_t i = 0; i < r.configurations.size(); ++i)
				d->fits.push_back(
					MakeFit(r, d->training, columns, r.configurations[i], d->states[i], meter));
		}
		// All choices freeze before the validation partition is forward evaluated.
		auto validation = Prepare(r, d->validation, meter, d->placement);
		d->baseline.validation =
			Evaluate(r, d->validation, validation, background, d->baselineState, meter);
		double best = d->baseline.validation.robust;
		for (size_t i = 0; i < d->fits.size(); ++i)
			if (d->fits[i].available)
			{
				auto &fit = d->fits[i];
				fit.validation = Evaluate(r, d->validation, validation, r.configurations[i],
										  d->states[i], meter);
				double objective = fit.validation.robust +
								   r.options.validationPenaltyPerFixture * fit.sources.size();
				if (fit.validation.available && (!policy || Active(fit, *policy)))
					best = std::min(best, objective);
			}
		if (best + r.options.validationEquivalentSlack + r.options.tolerance <
			d->baseline.validation.robust)
			for (size_t i = 0; i < d->fits.size(); ++i)
			{
				const auto &fit = d->fits[i];
				double objective = fit.validation.robust +
								   r.options.validationPenaltyPerFixture * fit.sources.size();
				if (fit.available && fit.validation.available && (!policy || Active(fit, *policy)) &&
					objective <= best + r.options.validationEquivalentSlack)
				{
					d->alternatives.push_back(i);
					if (d->selected == SIZE_MAX ||
						fit.sources.size() < d->fits[d->selected].sources.size() ||
						(policy && fit.sources.size() == d->fits[d->selected].sources.size() &&
						 (HiddenCount(*d, i) < HiddenCount(*d, d->selected) ||
						  (HiddenCount(*d, i) == HiddenCount(*d, d->selected) &&
						   r.configurations[i].fixtures < r.configurations[d->selected].fixtures))))
						d->selected = i;
				}
			}
		if (policy)
		{
			if (d->selected != SIZE_MAX)
			{
				const auto &chosen = r.configurations[d->selected].fixtures;
				double chosenObjective = d->fits[d->selected].validation.robust +
					r.options.validationPenaltyPerFixture * chosen.size();
				for (size_t i : d->alternatives)
				{
					const auto &fs = r.configurations[i].fixtures;
					double objective = d->fits[i].validation.robust +
						r.options.validationPenaltyPerFixture * fs.size();
					if (fs.size() > chosen.size() && std::includes(fs.begin(), fs.end(), chosen.begin(), chosen.end()) &&
						objective + r.options.validationEquivalentSlack >= chosenObjective)
						for (size_t f : fs)
							if (!std::binary_search(chosen.begin(), chosen.end(), f))
								Decision(d->sparse, r.fixtures[f].variants[0].geometry.source.id, i,
									"redundant-source-removed", "Frozen deletion/refit is validation-equivalent with fewer sources; redundant superset pruned");
				}
				// Equivalence alternatives describe compact competing explanations.
				// Redundant supersets remain in Fits() for reproducible loss diagnostics.
				d->alternatives.erase(std::remove_if(d->alternatives.begin(), d->alternatives.end(),
					[&](size_t i) { return d->fits[i].sources.size() > chosen.size(); }), d->alternatives.end());
			}
			SparseConclusion(*d);
		}
		d->work = meter.work;
		out = std::move(d);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Aesthetic fit allocation");
	}
}
} // namespace
Error FitAndValidate(const Request &input, const std::vector<Shape::Observation> &train,
					 const std::vector<Shape::Observation> &valid, const ReadContext &read,
					 Experiment &out)
{
	out = {};
	return RunExperiment(input, nullptr, train, valid, read, out.data);
}
Error SelectSparse(const Request &input, const SparsePolicy &policy,
				   const std::vector<Shape::Observation> &train,
				   const std::vector<Shape::Observation> &valid, const ReadContext &read,
				   Experiment &out)
{
	out = {};
	return RunExperiment(input, &policy, train, valid, read, out.data);
}
Error AuditHeldout(const Experiment &experiment, const std::vector<Shape::Observation> &held,
				   const ReadContext &read, std::vector<Score> &scores, Score &baseline)
{
	scores.clear();
	baseline = {};
	try
	{
		Need(bool(experiment.data), Code::InvalidState, "Frozen aesthetic experiment required");
		const auto &d = *experiment.data;
		auto rows = Inspect(held, Shape::Role::Heldout, d.request);
		Disjoint(rows, d.training);
		Disjoint(rows, d.validation);
		Meter meter(d.request.options, read);
		meter.Tick();
		auto columns = Prepare(d.request, rows, meter, d.placement);
		Configuration background;
		background.name = "no-local-light";
		Score base = Evaluate(d.request, rows, columns, background, d.baselineState, meter);
		std::vector<Score> result(d.fits.size());
		for (size_t i = 0; i < d.fits.size(); ++i)
			if (d.fits[i].available)
				result[i] = Evaluate(d.request, rows, columns, d.request.configurations[i],
									 d.states[i], meter);
		scores = std::move(result);
		baseline = base;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Aesthetic held-out allocation");
	}
}
} // namespace Aesthetic
} // namespace Rend2LightRecon
