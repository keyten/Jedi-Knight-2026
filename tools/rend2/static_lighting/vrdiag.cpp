// Offline diagnostics of tr_volrecon.cpp on real q3map light grids (export_grid.py).
// Includes the implementation so the anonymous namespace stages are reachable.
// Build (MSVC, from this directory): cl /nologo /O2 /EHsc /I..\..\..\shared\rd-rend2 vrdiag.cpp
// Run: vrdiag.exe <map>.vrg [full] [factors]   (export_grid.py writes <map>.vrg)
//   seeds and fit rejection reasons; "full" also runs VR_Reconstruct without traces;
//   a third argument breaks the attribution match into its factors per point source
#include "tr_volrecon.cpp"
#include <cstdio>
#include <map>
#include <string>

namespace
{

// FitSeed, copied with a reason for every rejection (same arithmetic)
enum { R_OK, R_SUPPORT, R_MAXQ, R_RAYS, R_SOLVE, R_DRIFT, R_CHROMA, R_PROFILE_BINS, R_FLAT, R_CONF, R_COUNT };
const char *reasonNames[R_COUNT] = { "ok", "support<4", "maxQ=0", "rays<4", "solve", "drift>6diag", "no chroma", "profile bins<2", "flat/zero profile", "confidence<0.2" };

struct FitDiag { float cRay, cGradient, cProfile, cChroma, conditioning, rms, drift, decay; int support, lit; };

int FitSeedDiag( Ctx& ctx, const Seed& seed, FitDiag& dg )
{
	memset(&dg, 0, sizeof(dg));
	const int ch = seed.channel;
	int sx, sy, sz;
	ctx.Coord(seed.cell, &sx, &sy, &sz);
	std::vector<int>& support = ctx.support;
	Flood(ctx, seed.cell, [&]( int j ) {
		int x, y, z;
		ctx.Coord(j, &x, &y, &z);
		const float d2 = (float)((x - sx) * (x - sx) + (y - sy) * (y - sy) + (z - sz) * (z - sz));
		return d2 <= SUPPORT_RADIUS * SUPPORT_RADIUS;
	}, SUPPORT_MAX, support);
	dg.support = (int)support.size();
	if ( support.size() < 4 ) return R_SUPPORT;
	float maxQ = 0.0f;
	for ( int j : support ) maxQ = std::max(maxQ, ctx.QChannel(j, ch));
	if ( maxQ <= 0.0f ) return R_MAXQ;
	std::vector<Ray>& rays = ctx.rays;
	rays.clear();
	for ( int j : support )
	{
		const V3 x = ctx.Position(j);
		const float q = ctx.QChannel(j, ch);
		if ( q > 0.0f ) { Ray r = { x, ctx.BspDir(j), q / maxQ }; rays.push_back(r); }
		if ( ctx.Strength(j, ch) > GRADIENT_MIN_STRENGTH )
		{
			float length;
			const V3 d = Normalize(ctx.GradChannel(j, ch), &length);
			Ray r = { x, d, length * ctx.diag / maxQ };
			rays.push_back(r);
		}
	}
	if ( rays.size() < 4 ) return R_RAYS;
	const V3 seedPos = ctx.Position(seed.cell);
	float wsum = 0.0f;
	for ( const Ray& r : rays ) wsum += r.weight;
	if ( wsum <= 1e-6f ) return R_RAYS;
	const float ridge = 1e-3f * wsum;
	const float delta = HUBER_DELTA * ctx.diag;
	std::vector<float> huber(rays.size(), 1.0f);
	V3 p = seedPos;
	M3 A;
	for ( int iteration = 0; iteration < 4; iteration++ )
	{
		Zero(A);
		V3 b = Scale(seedPos, ridge);
		for ( size_t k = 0; k < rays.size(); k++ )
		{
			const float w = rays[k].weight * huber[k];
			AddProjector(A, rays[k].dir, w);
			b = Add(b, Scale(Perpendicular(rays[k].dir, rays[k].origin), w));
		}
		M3 Ar = A;
		Ar.m[0] += ridge; Ar.m[4] += ridge; Ar.m[8] += ridge;
		if ( !Solve(Ar, b, &p) ) return R_SOLVE;
		for ( size_t k = 0; k < rays.size(); k++ )
		{
			const float r = Length(Perpendicular(rays[k].dir, Sub(p, rays[k].origin)));
			huber[k] = r <= delta ? 1.0f : delta / r;
		}
	}
	dg.drift = Length(Sub(p, seedPos)) / ctx.diag;
	if ( Length(Sub(p, seedPos)) > 6.0f * ctx.diag ) return R_DRIFT;
	Zero(A);
	for ( size_t k = 0; k < rays.size(); k++ ) AddProjector(A, rays[k].dir, rays[k].weight * huber[k]);
	double rss = 0.0, rw = 0.0;
	for ( size_t k = 0; k < rays.size(); k++ )
	{
		const float w = rays[k].weight * huber[k];
		const float r = Length(Perpendicular(rays[k].dir, Sub(p, rays[k].origin)));
		rss += w * r * r; rw += w;
	}
	const float rms = (float)sqrt(rss / std::max(rw, 1e-12));
	dg.rms = rms / ctx.diag;
	float lambdaMin, lambdaMax;
	Eigenvalues(A, &lambdaMin, &lambdaMax);
	dg.conditioning = lambdaMax > 1e-12f ? Saturate((lambdaMin / lambdaMax) / CONDITION_THRESHOLD) : 0.0f;
	float consistent = 0.0f, consistentWeight = 0.0f;
	for ( size_t k = 0; k < rays.size(); k++ )
	{
		const V3 u = Normalize(Sub(p, rays[k].origin));
		consistent += rays[k].weight * (Dot(rays[k].dir, u) > 0.25f ? 1.0f : 0.0f);
		consistentWeight += rays[k].weight;
	}
	dg.cGradient = consistentWeight > 0.0f ? consistent / consistentWeight : 0.0f;
	std::vector<float> distances;
	for ( int j : support ) distances.push_back(Length(Sub(p, ctx.Position(j))));
	std::vector<float> sorted = distances;
	std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
	const float medianDistance = sorted[sorted.size() / 2];
	V3 chromaNear = Make(0, 0, 0), chromaFar = chromaNear, chromaDirect = chromaNear;
	for ( size_t s = 0; s < support.size(); s++ )
	{
		const int j = support[s];
		const V3 u = Normalize(Sub(p, ctx.Position(j)));
		const V3 q = Make(std::max(Dot(ctx.grad[0][j], u), 0.0f), std::max(Dot(ctx.grad[1][j], u), 0.0f), std::max(Dot(ctx.grad[2][j], u), 0.0f));
		if ( distances[s] <= medianDistance ) chromaNear = Add(chromaNear, q); else chromaFar = Add(chromaFar, q);
		chromaDirect = Add(chromaDirect, Scale(Load(&ctx.Q[j * 3]), std::max(Dot(ctx.BspDir(j), u), 0.0f)));
	}
	V3 chroma = Add(chromaNear, chromaFar);
	if ( MaxComponent(chroma) > 1e-9f )
	{
		const V3 n = Normalize(chromaNear), f = Normalize(chromaFar);
		dg.cChroma = (Length(n) > 0.0f && Length(f) > 0.0f) ? Saturate((Dot(n, f) - 0.8f) / 0.2f) : 0.5f;
	}
	else { chroma = chromaDirect; dg.cChroma = 0.5f; }
	if ( MaxComponent(chroma) <= 1e-9f ) return R_CHROMA;
	const V3 chromaUnit = Normalize(chroma);
	const float binWidth = 0.5f * ctx.diag;
	std::vector<float> bins[PROFILE_BINS];
	std::vector<float> values(support.size());
	float maxValue = 0.0f;
	for ( size_t s = 0; s < support.size(); s++ ) { values[s] = std::max(Dot(Load(&ctx.Q[support[s] * 3]), chromaUnit), 0.0f); maxValue = std::max(maxValue, values[s]); }
	const float litValue = PROFILE_LIT * maxValue;
	for ( size_t s = 0; s < support.size(); s++ )
	{
		if ( values[s] < litValue ) continue;
		dg.lit++;
		bins[std::min(PROFILE_BINS - 1, (int)(distances[s] / binWidth))].push_back(values[s]);
	}
	float binR[PROFILE_BINS], binV[PROFILE_BINS], binW[PROFILE_BINS], iso[PROFILE_BINS];
	int binIndex[PROFILE_BINS];
	int count = 0;
	for ( int k = 0; k < PROFILE_BINS; k++ )
	{
		if ( bins[k].empty() ) continue;
		std::vector<float>& b = bins[k];
		std::nth_element(b.begin(), b.begin() + b.size() / 2, b.end());
		binR[count] = (k + 0.5f) * binWidth; binV[count] = b[b.size() / 2]; binW[count] = (float)b.size(); binIndex[k] = count; count++;
	}
	if ( count < 2 ) return R_PROFILE_BINS;
	IsotonicDecreasing(binV, binW, count, iso);
	double misfit = 0.0, total = 0.0;
	for ( size_t s = 0; s < support.size(); s++ )
	{
		if ( values[s] < litValue ) continue;
		const int bin = std::min(PROFILE_BINS - 1, (int)(distances[s] / binWidth));
		misfit += fabsf(values[s] - iso[binIndex[bin]]); total += values[s];
	}
	if ( total <= 1e-9 || iso[0] <= 1e-9f ) return R_FLAT;
	dg.decay = 1.0f - iso[count - 1] / iso[0];
	dg.cProfile = Saturate(1.0f - (float)(misfit / total)) * Saturate(dg.decay / 0.3f);
	dg.cRay = expf(-(rms / (SIGMA_RAY * ctx.diag)) * (rms / (SIGMA_RAY * ctx.diag)));
	const float confidence = dg.cRay * sqrtf(dg.cGradient * dg.cProfile * dg.cChroma);
	if ( !(confidence >= MIN_SOURCE_CONFIDENCE) ) return R_CONF;
	return R_OK;
}

} // namespace

int main( int argc, char **argv )
{
	if ( argc < 2 ) { printf("vrdiag map.vrg [full]\n"); return 1; }
	FILE *f = fopen(argv[1], "rb");
	if ( !f ) return 1;
	int dims[3]; float origin[3], size[3], sun[4];
	fread(dims, 4, 3, f); fread(origin, 4, 3, f); fread(size, 4, 3, f); fread(sun, 4, 4, f);
	const int n = dims[0] * dims[1] * dims[2];
	std::vector<float> A(n * 3), D(n * 3), dir(n * 3);
	std::vector<uint8_t> valid(n), anim(n);
	fread(A.data(), 4, n * 3, f); fread(D.data(), 4, n * 3, f); fread(dir.data(), 4, n * 3, f);
	fread(valid.data(), 1, n, f); fread(anim.data(), 1, n, f);
	fclose(f);

	vrInput in;
	memset(&in, 0, sizeof(in));
	for ( int c = 0; c < 3; c++ ) { in.dims[c] = dims[c]; in.origin[c] = origin[c]; in.cellSize[c] = size[c]; }
	in.hdr = false; in.mode = 1;
	in.ambient = A.data(); in.direct = D.data(); in.bspDir = dir.data(); in.valid = valid.data(); in.animated = anim.data();

	// the same set up as VR_Reconstruct up to the seeds
	Ctx ctx;
	ctx.in = &in; ctx.bx = dims[0]; ctx.by = dims[1]; ctx.bz = dims[2]; ctx.n = n;
	ctx.origin = Load(in.origin); ctx.size = Load(in.cellSize); ctx.diag = Length(ctx.size);
	ctx.currentStamp = 0; ctx.traces = 0; ctx.portalTraces = 0;
	vrOutput out;
	out.baseline.assign(n * 3, 0.0f);
	ctx.Q.assign(n * 3, 0.0f);
	int validCells = 0;
	for ( int i = 0; i < n; i++ )
	{
		const bool v = valid[i] != 0;
		if ( v ) validCells++;
		for ( int c = 0; c < 3; c++ )
		{
			const float a = std::max(A[i * 3 + c], 0.0f), d = std::max(D[i * 3 + c], 0.0f);
			const float legacy = std::max(a, d);
			out.baseline[i * 3 + c] = legacy;
			if ( v ) ctx.Q[i * 3 + c] = std::min(std::max(d - a, 0.0f), legacy);
		}
	}
	ctx.B = out.baseline.data();
	double channelSum[4] = { 0, 0, 0, 0 };
	for ( int i = 0; i < n; i++ ) if ( ctx.Valid(i) ) for ( int ch = 0; ch < 4; ch++ ) channelSum[ch] += ctx.BChannel(i, ch);
	for ( int ch = 0; ch < 4; ch++ ) ctx.eps[ch] = 0.02f * (float)(channelSum[ch] / std::max(1, validCells)) + 1e-6f;
	ctx.meanLuma = (float)(channelSum[3] / std::max(1, validCells));
	ctx.stamp.assign(n, 0);
	ctx.positions.resize(n);
	for ( int i = 0; i < n; i++ ) { int x, y, z; ctx.Coord(i, &x, &y, &z); ctx.positions[i] = Make(ctx.origin.x + x * ctx.size.x, ctx.origin.y + y * ctx.size.y, ctx.origin.z + z * ctx.size.z); }
	ComputeGradients(ctx);
	printf("grid %d x %d x %d = %d cells, cell %g %g %g diag %.1f, meanLuma %.4f eps %.4f\n", dims[0], dims[1], dims[2], n, size[0], size[1], size[2], ctx.diag, ctx.meanLuma, ctx.eps[3]);

	// strength distribution of lit cells
	{
		int lit = 0, strong = 0, saturated = 0, blackNb = 0;
		for ( int i = 0; i < n; i++ )
		{
			if ( ctx.BChannel(i, 3) <= 0.0f ) continue;
			lit++;
			const float s = ctx.Strength(i, 3);
			if ( s > GRADIENT_MIN_STRENGTH ) strong++;
			if ( s >= 0.999f ) saturated++;
			int x, y, z; ctx.Coord(i, &x, &y, &z);
			bool nb = false;
			for ( int k = 0; k < 6 && !nb; k++ )
			{
				int c[3] = { x, y, z }; c[k / 2] += (k & 1) ? 1 : -1;
				if ( c[0] < 0 || c[1] < 0 || c[2] < 0 || c[0] >= ctx.bx || c[1] >= ctx.by || c[2] >= ctx.bz ) continue;
				if ( ctx.BChannel(ctx.Index(c[0], c[1], c[2]), 3) <= 0.0f ) nb = true;
			}
			if ( nb ) blackNb++;
		}
		printf("lit cells %d: gradient strength > %.2f %.1f%%, saturated (=1) %.1f%%, next to a black (wall) cell %.1f%%\n", lit, GRADIENT_MIN_STRENGTH,
			100.0 * strong / std::max(lit, 1), 100.0 * saturated / std::max(lit, 1), 100.0 * blackNb / std::max(lit, 1));
	}

	std::vector<Seed> seeds;
	FindSeeds(ctx, seeds);
	int reasons[R_COUNT] = {}, perChannel[4] = {};
	double sum[R_COUNT][8] = {};
	int lowFactor[4] = {};	// for R_CONF: the weakest factor (cRay, cGradient, cProfile, cChroma)
	int seedsNextToWall = 0;
	for ( const Seed& s : seeds )
	{
		perChannel[s.channel]++;
		FitDiag dg;
		const int r = FitSeedDiag(ctx, s, dg);
		reasons[r]++;
		const float v[8] = { dg.cRay, dg.cGradient, dg.cProfile, dg.cChroma, dg.conditioning, dg.rms, dg.drift, dg.decay };
		for ( int k = 0; k < 8; k++ ) sum[r][k] += v[k];
		if ( r == R_CONF )
		{
			const float fs[4] = { dg.cRay, dg.cGradient, dg.cProfile, dg.cChroma };
			int w = 0;
			for ( int k = 1; k < 4; k++ ) if ( fs[k] < fs[w] ) w = k;
			lowFactor[w]++;
		}
		int x, y, z; ctx.Coord(s.cell, &x, &y, &z);
		bool nb = false;
		for ( int dz = -1; dz <= 1; dz++ ) for ( int dy = -1; dy <= 1; dy++ ) for ( int dx = -1; dx <= 1; dx++ )
		{
			const int a = x + dx, b = y + dy, c = z + dz;
			if ( a < 0 || b < 0 || c < 0 || a >= ctx.bx || b >= ctx.by || c >= ctx.bz ) continue;
			if ( ctx.BChannel(ctx.Index(a, b, c), 3) <= 0.0f ) nb = true;
		}
		if ( nb ) seedsNextToWall++;
	}
	printf("seeds %d (R %d G %d B %d luma %d), %d touch a black cell\n", (int)seeds.size(), perChannel[0], perChannel[1], perChannel[2], perChannel[3], seedsNextToWall);
	if ( !seeds.empty() )
		printf("  score range %.4f .. %.4f\n", seeds.front().score, seeds.back().score);
	for ( int r = 0; r < R_COUNT; r++ )
	{
		if ( !reasons[r] ) continue;
		const double k = 1.0 / reasons[r];
		printf("  %-18s %4d   mean cRay %.2f cGrad %.2f cProf %.2f cChroma %.2f cond %.2f rms %.2f drift %.2f decay %.2f\n", reasonNames[r], reasons[r],
			sum[r][0] * k, sum[r][1] * k, sum[r][2] * k, sum[r][3] * k, sum[r][4] * k, sum[r][5] * k, sum[r][6] * k, sum[r][7] * k);
	}
	printf("  confidence rejects, weakest factor: cRay %d cGradient %d cProfile %d cChroma %d\n", lowFactor[0], lowFactor[1], lowFactor[2], lowFactor[3]);

	if ( argc > 2 )
	{
		// the full reconstruction without traces (no structured lights): attribution
		VR_Reconstruct(in, out);
		const vrStats& st = out.stats;
		printf("full: seeds %d fits %d merged %d points %d dropped %d attributed %.2f%% directional %.2f%% cells %d msec %.0f (grad %.0f seeds %.0f fit %.0f attr %.0f)\n",
			st.seeds, st.fits, st.mergedFits, st.pointProxies, st.droppedSources, st.attributedFraction * 100.0f, st.directionalFraction * 100.0f,
			st.directionalCells, st.msecTotal, st.msecGradients, st.msecSeeds, st.msecFit, st.msecAttribution);
	}
	if ( argc > 3 )
	{
		// rebuild the point sources as Reconstruct does and break the attribution match into its factors
		std::vector<Source> fitted;
		for ( const Seed& s : seeds ) { Source so; float rms; if ( FitSeed(ctx, s, &so, &rms) ) fitted.push_back(so); }
		std::stable_sort(fitted.begin(), fitted.end(), []( const Source& a, const Source& b ) { return a.proxy.confidence * a.energy > b.proxy.confidence * b.energy; });
		std::vector<Source> points;
		for ( const Source& s : fitted )
		{
			bool merged = false;
			for ( Source& o : points )
			{
				const float distance = std::min(MERGE_DISTANCE_MAX * ctx.diag, std::max(MERGE_DISTANCE * ctx.diag, o.proxy.sigmaP + s.proxy.sigmaP));
				if ( Length(Sub(o.position, s.position)) < distance && Dot(o.chroma, s.chroma) > MERGE_CHROMA_COS ) { o.proxy.range = std::max(o.proxy.range, s.proxy.range); o.energy += s.energy; merged = true; break; }
			}
			if ( !merged ) points.push_back(s);
		}
		double totQ = 0; for ( int i = 0; i < n; i++ ) totQ += Luma(&ctx.Q[i * 3]);
		double sDomQ = 0, sMQ = 0, sFall = 0, sBsp = 0, sComp = 0, sConf = 0, sCpos = 0, cnt = 0;
		int fallLow = 0, bspLow = 0;
		printf("points %d, budget Q total %.1f\n", (int)points.size(), totQ);
		for ( size_t k = 0; k < points.size(); k++ )
		{
			const Source& src = points[k];
			const float range2 = src.proxy.range * src.proxy.range;
			Flood(ctx, src.startCell, [&]( int j ) { const V3 d = Sub(ctx.Position(j), src.position); return Dot(d, d) <= range2; }, DOMAIN_MAX, ctx.domain);
			double domQ = 0, mQ = 0, fall = 0, bsp = 0, wsum = 0;
			for ( int i : ctx.domain )
			{
				const float *q = &ctx.Q[i * 3];
				const float lq = Luma(q);
				if ( lq <= 0 ) continue;
				float r; const V3 u = Normalize(Sub(src.position, ctx.Position(i)), &r);
				const float s2 = src.proxy.sigmaP * src.proxy.sigmaP;
				const float cPos = r * r / (r * r + s2);
				const float predicted = ProfileAt(src, r);
				const float observed = std::max(Dot(Load(q), src.chroma), 1e-9f);
				const float falloff = Saturate(predicted / observed);
				const float b = Saturate(2.0f * Dot(ctx.BspDir(i), u) - 1.0f);
				const float m = src.proxy.confidence * cPos * falloff * b;
				domQ += lq; mQ += lq * m; fall += lq * falloff; bsp += lq * b; wsum += lq;
				if ( falloff < 0.2f ) fallLow++;
				if ( b < 0.2f ) bspLow++;
				cnt++;
			}
			sDomQ += domQ; sMQ += mQ; sFall += fall; sBsp += bsp; sConf += src.proxy.confidence * wsum; sCpos += wsum;
			if ( k < 12 )
				printf("  src %2d conf %.2f sigma %.2f range %.2f diag, domain %d cells, Q in domain %.1f, Q*m %.2f, mean falloff %.2f, mean bsp agreement %.2f\n", (int)k,
					src.proxy.confidence, src.proxy.sigmaP / ctx.diag, src.proxy.range / ctx.diag, (int)ctx.domain.size(), domQ, mQ, wsum > 0 ? fall / wsum : 0, wsum > 0 ? bsp / wsum : 0);
		}
		printf("all points: domains cover %.1f%% of Q (with overlap), m-weighted %.2f%%; Q-weighted mean falloff %.2f, bsp agreement %.2f, confidence %.2f; cells with falloff<0.2 %.0f%%, bsp<0.2 %.0f%%\n",
			100 * sDomQ / totQ, 100 * sMQ / totQ, sFall / sCpos, sBsp / sCpos, sConf / sCpos, 100.0 * fallLow / cnt, 100.0 * bspLow / cnt);
	}
	return 0;
}
