// Exact planar shadow clipping + deterministic smooth-response quadrature.
// Separate SI reference; never used to impersonate q3map super1 sampling.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bake_math.h"
#include <functional>
namespace Rend2LightRecon
{
namespace Core
{
namespace Bake
{
using namespace Detail;
namespace
{
using Poly = std::vector<V>;
double Area(const Poly &p)
{
	if (p.size() < 3)
		return 0;
	V sum;
	for (size_t i = 1; i + 1 < p.size(); ++i)
		sum = sum + Cross(p[i] - p[0], p[i + 1] - p[0]);
	return Length(sum) * .5;
}
Poly Clip(const Poly &p, V a, V b, V normal, bool inside)
{
	Poly out;
	if (p.empty())
		return out;
	V edge = b - a;
	for (size_t i = 0; i < p.size(); ++i)
	{
		V x = p[i], y = p[(i + 1) % p.size()];
		double f = Dot(Cross(edge, x - a), normal), g = Dot(Cross(edge, y - a), normal);
		bool keep = inside ? f >= 0 : f <= 0;
		if (keep)
			out.push_back(x);
		if ((f < 0 && g > 0) || (f > 0 && g < 0))
			out.push_back(x + (y - x) * (f / (f - g)));
	}
	return out;
}
std::vector<Poly> Subtract(const Poly &p, Poly shadow, V n)
{
	std::vector<Poly> out;
	if (Area(shadow) < 1e-16)
	{
		out.push_back(p);
		return out;
	}
	if (Dot(Cross(shadow[1] - shadow[0], shadow[2] - shadow[0]), n) < 0)
		std::reverse(shadow.begin(), shadow.end());
	Poly remaining = p;
	for (size_t i = 0; i < shadow.size() && !remaining.empty(); ++i)
	{
		Poly outside = Clip(remaining, shadow[i], shadow[(i + 1) % shadow.size()], n, false);
		if (Area(outside) > 1e-16)
			out.push_back(outside);
		remaining = Clip(remaining, shadow[i], shadow[(i + 1) % shadow.size()], n, true);
	}
	return out;
}
struct Node
{
	V position, incoming, normal, rgb;
	double measure = 1;
	bool directional = false, area = false;
};
struct Integrator
{
	const Options &o;
	const ReadContext &read;
	Work work;
	bool uncertain = false;
	double error = 0;
	Integrator(const Options &options, const ReadContext &context) : o(options), read(context) {}
	void Step()
	{
		if (Cancelled(read))
			throw Failure(Code::Cancelled, "Physical integral cancelled");
		if (++work.quadrature + work.triangleTests > o.maxWork)
			throw Failure(Code::WorkLimit, "Physical integral budget");
	}
	double Response(V p, V n, const Node &node, const SourceHypothesis &s)
	{
		Step();
		V delta = node.directional ? node.incoming : node.position - p;
		double r = Length(delta);
		if (r < 1e-10)
			throw Failure(Code::InvalidInput, "Singular physical source distance");
		V u = delta / r;
		double cosine = Dot(n, u);
		if (cosine < 0)
			cosine = 0;
		double value = cosine;
		if (!node.directional)
			value *= double(s.worldUnits.worldUnitsPerMeter) * s.worldUnits.worldUnitsPerMeter / (r * r);
		if (node.area)
			value *= s.angular.twoSided ? std::abs(Dot(node.normal, u)) : std::max(0.0, -Dot(node.normal, u));
		if (s.kind == SourceKind::Spot)
		{
			double t = Clamp((-Dot(V(s.orientation.forward), u) - s.angular.cosOuter) /
								 (s.angular.cosInner - s.angular.cosOuter),
							 0, 1);
			value *= t * t * (3 - 2 * t);
		}
		return value;
	}
	double TriangleIntegral(V a, V b, V c, V normal, const Node &node, const SourceHypothesis &s,
							unsigned depth)
	{
		double area = Length(Cross(b - a, c - a)) * .5;
		if (area < 1e-18)
			return 0;
		V ab = (a + b) * .5, bc = (b + c) * .5, ca = (c + a) * .5;
		double coarse = Response((a + b + c) / 3, normal, node, s) * area;
		double fine =
			(Response((a + ab + ca) / 3, normal, node, s) + Response((ab + b + bc) / 3, normal, node, s) +
			 Response((ca + bc + c) / 3, normal, node, s) + Response((ab + bc + ca) / 3, normal, node, s)) *
			area * .25;
		double discrepancy = std::abs(fine - coarse);
		if (discrepancy <= o.integralTolerance * area || depth >= o.quadratureDepth)
		{
			error += discrepancy * Peak(node.rgb);
			return fine;
		}
		return TriangleIntegral(a, ab, ca, normal, node, s, depth + 1) +
			   TriangleIntegral(ab, b, bc, normal, node, s, depth + 1) +
			   TriangleIntegral(ca, bc, c, normal, node, s, depth + 1) +
			   TriangleIntegral(ab, bc, ca, normal, node, s, depth + 1);
	}
};
} // namespace
Error PhysicalReference(const Chunk<SourceHypothesis> &sources, const Receiver &r,
						const EvaluationContext &context, const Options &o, const ReadContext &read,
						Prediction &out)
{
	out = Prediction();
	out.observation = r.sample;
	out.domain = Domain::PhysicalIrradianceLux;
	out.unit = PhotometricUnit::Lux;
	out.fidelity = Fidelity::PhysicalIntegral;
	if (o.mode != Mode::PhysicalIntegral)
		return Failure(Code::InvalidInput, "Explicit PhysicalIntegral mode required");
	if (r.geometryErrorWorld > 0)
	{
		out.status = Status::UnsupportedFeature;
		out.reason = "Receiver geometry envelope is not a certified planar footprint";
		return Error();
	}
	if (sources.count > 4096 || (sources.count && (!sources.owner || !sources.data)) || !r.sample ||
		!Finite(r.world) || !Normal(r.geometricNormal) || !Normal(r.shadingNormal))
		return Failure(Code::InvalidInput, "Invalid physical reference input lease/receiver");
	if (!context.integralScene || !context.integralScene->complete)
	{
		out.status = Status::VisibilityUnknown;
		out.reason = "Local integral scene coverage is not certified";
		return Error();
	}
	const auto &scene = *context.integralScene;
	if ((scene.triangles.count && (!scene.triangles.owner || !scene.triangles.data)) || !r.footprint.owner ||
		!r.footprint.count || r.footprint.count > 4096 || !r.footprint.data)
		return Failure(Code::InvalidInput, "Physical reference requires leased exact footprint pieces");
	if (scene.triangles.count > 1048576 ||
		(read.maxBytes && r.footprint.count * sizeof(FootprintTriangle) > read.maxBytes))
		return Failure(Code::MemoryLimit, "Physical reference local input budget");
	if (!std::isfinite(o.integralTolerance) || o.integralTolerance <= 0 || o.quadratureDepth > 12)
		return Failure(Code::InvalidInput, "Integral numerical settings");
	try
	{
		Integrator integrate{o, read};
		V total, localEmitters, sun;
		double visible = 0, areaTotal = 0, measureTotal = 0;
		bool finiteArea = false;
		for (size_t j = 0; j < sources.count; ++j)
		{
			const auto &s = sources.data[j];
			if ((s.position.sigmaWorld.known && s.position.sigmaWorld.value > 0) ||
				s.canonicalEmission.intensity.bounded)
				out.issues |= SourceParameterUncertainty;
			const auto &e = s.canonicalEmission;
			if (s.amplitudeDomain != AmplitudeDomain::CanonicalPhotometry ||
				s.calibration.kind != CalibrationKind::PhysicallyAuthored || !s.calibration.id ||
				e.calibration != s.calibration.id || !e.intensity.known || !e.color.known ||
				!Positive(e.color.rgb) || !std::isfinite(e.intensity.value) || e.intensity.value < 0 ||
				std::abs(Dot(V(e.color.rgb), V(.2126, .7152, .0722)) - 1) > 1e-4 ||
				!std::isfinite(s.worldUnits.worldUnitsPerMeter) || s.worldUnits.worldUnitsPerMeter <= 0)
				return Failure(Code::InvalidInput,
							   "SI reference requires authored calibrated Y=1 photometry");
			if (s.kind != SourceKind::Point && s.kind != SourceKind::Spot &&
				s.kind != SourceKind::Directional && s.kind != SourceKind::Polygon)
			{
				out.status = Status::UnsupportedFeature;
				out.reason = "No physical reference for this source kind";
				return Error();
			}
			bool directional = s.kind == SourceKind::Directional, rectangle = s.kind == SourceKind::Polygon;
			if ((s.kind == SourceKind::Point && s.angular.kind != AngularKind::Isotropic) ||
				(directional && s.angular.kind != AngularKind::Parallel) ||
				(rectangle && s.angular.kind != AngularKind::Lambertian))
			{
				out.status = Status::UnsupportedFeature;
				out.reason = "Explicit physical angular-emission law is required";
				return Error();
			}
			if (e.unit != (directional ? PhotometricUnit::Lux
									   : rectangle ? PhotometricUnit::Nits : PhotometricUnit::Candela))
				return Failure(Code::InvalidInput, "SI source photometric unit");
			if (!directional && (!s.position.known || !Finite(s.position.world)))
				return Failure(Code::InvalidInput, "Physical source position");
			if ((directional || rectangle || s.kind == SourceKind::Spot) &&
				(!s.orientation.known || !Normal(s.orientation.forward)))
				return Failure(Code::InvalidInput, "Physical source direction");
			if ((s.geometry.effectiveRadiusWorld.known && s.geometry.effectiveRadiusWorld.value > 0) ||
				(directional && s.geometry.angularRadiusRadians.known &&
				 s.geometry.angularRadiusRadians.value > 0))
			{
				out.status = Status::UnsupportedFeature;
				out.reason = "Finite sphere/angular disk reference is not implemented";
				return Error();
			}
			if (s.kind == SourceKind::Spot &&
				(s.angular.kind != AngularKind::SmoothCone || s.angular.cosOuter >= s.angular.cosInner ||
				 s.angular.cosOuter < -1 || s.angular.cosInner > 1 || !std::isfinite(s.angular.cosOuter) ||
				 !std::isfinite(s.angular.cosInner)))
				return Failure(Code::InvalidInput, "Physical spot cone");
			std::vector<Node> nodes;
			Node node;
			node.position = V(s.position.world);
			node.incoming = V(s.orientation.forward) * -1;
			node.directional = directional;
			node.rgb = V(e.color.rgb) * e.intensity.value;
			if (rectangle)
			{
				if (!s.geometry.known || s.geometry.halfWidthWorld <= 0 || s.geometry.halfHeightWorld <= 0 ||
					!std::isfinite(s.geometry.halfWidthWorld) || !std::isfinite(s.geometry.halfHeightWorld) ||
					!Normal(s.orientation.right) || !Normal(s.orientation.up) ||
					Length(Cross(V(s.orientation.right), V(s.orientation.up)) - V(s.orientation.forward)) >
						1e-4)
					return Failure(Code::InvalidInput, "Physical rectangle dimensions/frame");
				finiteArea = true;
				node.area = true;
				node.normal = V(s.orientation.forward);
				double area = 4 * double(s.geometry.halfWidthWorld) * s.geometry.halfHeightWorld /
							  (double(s.worldUnits.worldUnitsPerMeter) * s.worldUnits.worldUnitsPerMeter);
				// Deterministic midpoint source quadrature. Finite-source discretization error
				// remains UNKNOWN; never advertise the receiver-only estimate as total error.
				for (int x = 0; x < 8; ++x)
					for (int y = 0; y < 8; ++y)
					{
						Node n = node;
						n.measure = 1.0 / 64;
						n.rgb = node.rgb * (area / 64);
						n.position =
							node.position +
							V(s.orientation.right) * (2 * s.geometry.halfWidthWorld * ((x + .5) / 8 - .5)) +
							V(s.orientation.up) * (2 * s.geometry.halfHeightWorld * ((y + .5) / 8 - .5));
						nodes.push_back(n);
					}
			}
			else
				nodes.push_back(node);
			for (const Node &n : nodes)
			{
				measureTotal += n.measure;
				for (size_t k = 0; k < r.footprint.count; ++k)
				{
					const auto &ft = r.footprint.data[k];
					Poly piece;
					for (Vec3 v : ft.world)
					{
						if (!Finite(v))
							return Failure(Code::InvalidInput, "Footprint vertex");
						piece.push_back(V(v));
					}
					double area = Area(piece);
					if (area < 1e-14)
						return Failure(Code::InvalidInput, "Degenerate footprint");
					V normal = Unit(Cross(piece[1] - piece[0], piece[2] - piece[0]));
					if (Dot(normal, V(r.geometricNormal)) < 0)
						normal = normal * -1;
					for (Vec3 sn : ft.normal)
						if (!Normal(sn) || Length(V(sn) - V(r.shadingNormal)) > 1e-5)
						{
							out.status = Status::UnsupportedFeature;
							out.reason =
								"Varying curved footprint normals are not integrated by the planar reference";
							return Error();
						}
					V shading(r.shadingNormal);
					if (r.twoSided && Dot(shading, n.directional ? n.incoming : n.position - piece[0]) < 0)
						shading = shading * -1;
					if (j == 0 && &n == &nodes[0])
						areaTotal += area;
					std::vector<Poly> pieces(1, piece);
					V anchor = piece[0];
					for (size_t ti = 0; ti < scene.triangles.count && !pieces.empty(); ++ti)
					{
						integrate.Step();
						++integrate.work.triangleTests;
						const auto &t = scene.triangles.data[ti];
						Poly shadow;
						double h[3];
						for (int z = 0; z < 3; ++z)
						{
							V v(t.triangle.verticesWorld[z]);
							if (!Finite(v))
								return Failure(Code::InvalidInput, "Integral geometry vertex");
							h[z] = Dot(v - anchor, normal);
						}
						double lo = std::min(h[0], std::min(h[1], h[2])),
							   hi = std::max(h[0], std::max(h[1], h[2]));
						if (hi <= 1e-9)
							continue;
						double H = n.directional ? 1 : Dot(n.position - anchor, normal),
							   denom = n.directional ? Dot(n.incoming, normal) : H;
						if (lo < -1e-9 || denom <= 1e-10 || (!n.directional && lo < H && hi >= H))
						{
							integrate.uncertain = true;
							continue;
						}
						if (!n.directional && lo >= H)
							continue;
						for (int z = 0; z < 3; ++z)
						{
							V v(t.triangle.verticesWorld[z]);
							shadow.push_back(n.directional
												 ? v - n.incoming * (h[z] / denom)
												 : n.position + (v - n.position) * (H / (H - h[z])));
						}
						if (t.optical != ObstacleKind::Opaque)
						{
							if (t.optical != ObstacleKind::Sky)
							{
								for (const Poly &p : pieces)
								{
									Poly intersect = p;
									V orient = Cross(shadow[1] - shadow[0], shadow[2] - shadow[0]);
									if (Dot(orient, normal) < 0)
										std::reverse(shadow.begin(), shadow.end());
									for (size_t q = 0; q < shadow.size(); ++q)
										intersect = Clip(intersect, shadow[q],
														 shadow[(q + 1) % shadow.size()], normal, true);
									if (Area(intersect) > 1e-12)
										integrate.uncertain = true;
								}
							}
							continue;
						}
						std::vector<Poly> remaining;
						for (const auto &p : pieces)
						{
							auto cut = Subtract(p, shadow, normal);
							remaining.insert(remaining.end(), cut.begin(), cut.end());
							if (remaining.size() > 4096)
								throw Failure(Code::MemoryLimit, "Integral polygon fragment limit");
						}
						pieces.swap(remaining);
					}
					for (const Poly &p : pieces)
					{
						visible += Area(p) * n.measure;
						for (size_t z = 1; z + 1 < p.size(); ++z)
						{
							V contribution =
								n.rgb * integrate.TriangleIntegral(p[0], p[z], p[z + 1], shading, n, s, 0);
							total = total + contribution;
							if (s.kind == SourceKind::Directional)
								sun = sun + contribution;
							else
								localEmitters = localEmitters + contribution;
						}
					}
				}
			}
		}
		if (sources.count == 0)
		{
			for (size_t k = 0; k < r.footprint.count; ++k)
			{
				Poly p;
				for (Vec3 v : r.footprint.data[k].world)
					p.push_back(V(v));
				areaTotal += Area(p);
			}
			measureTotal = 1;
			visible = areaTotal;
		}
		out.work = integrate.work;
		if (integrate.uncertain)
		{
			out.status = Status::VisibilityUnknown;
			out.reason = "Unknown optical/projection topology affects this footprint";
			return Error();
		}
		if (areaTotal <= 0)
			return Failure(Code::InvalidInput, "Zero footprint area");
		total = total / areaTotal;
		if (!Positive(total) || Peak(total) > std::numeric_limits<float>::max())
			return Failure(Code::InvalidInput, "Physical integral overflow");
		out.rgb = total.Float();
		out.direct = (localEmitters / areaTotal).Float();
		out.sun = (sun / areaTotal).Float();
		out.available = true;
		out.status = Status::AvailablePhysical;
		out.integratedVisibility.known = true;
		out.integratedVisibility.value = float(visible / (areaTotal * measureTotal));
		if (!finiteArea)
		{
			out.numericalError.known = true;
			out.numericalError.value = float(integrate.error / areaTotal);
		}
		if (finiteArea)
			out.issues |= ApproximationWithoutTotalErrorBound;
		out.reason = finiteArea
						 ? "Planar boundary clipping; 8x8 source quadrature, total numerical error unbounded"
						 : "Exact planar shadow clipping; adaptive smooth quadrature error estimate (not "
						   "rigorous bound)";
		return Error();
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Physical integral allocation");
	}
}
} // namespace Bake
} // namespace Core
} // namespace Rend2LightRecon
