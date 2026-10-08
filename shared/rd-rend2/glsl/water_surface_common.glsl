/*[Vertex]*/
// Shared analytic water-surface state.  Both the visible water program and
// surface-driven caustics call this exact evaluator; callers supply the
// already-resolved body/profile parameters and add appropriately filtered
// texture/interaction slopes afterwards.
struct WaterSurfaceCommonState
{
	vec3 displacement;
	vec2 slope;
	vec3 velocity;
	float height;
	float attenuation;
	float curvature;
};

WaterSurfaceCommonState EvaluateWaterSurfaceCommon(vec3 worldPosition, float time,
	vec4 controls, vec4 profile, vec4 body, vec4 flow, vec4 flowDetail, vec4 terms[8])
{
	WaterSurfaceCommonState w;
	w.displacement = vec3(0.0); w.slope = vec2(0.0); w.velocity = vec3(0.0);
	w.height = 0.0; w.curvature = 0.0; w.attenuation = 1.0;
	if (controls.x < 0.5) return w;
	float speed = profile.z * controls.w * 16.0;
	if (flowDetail.z > 0.5 && body.z > 0.0) w.attenuation = smoothstep(0.0, 32.0, body.z);
	int count = body.y < 0.5 ? 2 : (body.y < 1.5 ? 4 : 8);
	float qualityAmplitude = count == 2 ? 1.86 : (count == 8 ? 0.59 : 1.0);
	float j00 = 1.0, j01 = 0.0, j11 = 1.0;
	for (int i = 0; i < 8; ++i)
	{
		if (i >= count) break;
		int component = count == 8 ? i : (i < count / 2 ? i : 4 + i - count / 2);
		float fi = float(component); vec4 term = terms[component]; vec2 direction = term.xy;
		float a = term.z * w.attenuation * qualityAmplitude; float k = term.w;
		float omega = speed * k; vec2 phasePosition = worldPosition.xy;
		if (flow.w > 0.5 && component >= 4) phasePosition -= flow.xy * time * flowDetail.y * 0.35;
		float phase = k * dot(direction, phasePosition) - omega * time + fi * 1.37;
		float sn = sin(phase), cs = cos(phase);
		w.height += a * sn; w.slope += a * k * cs * direction; w.velocity.z -= a * omega * cs;
		w.curvature -= a * k * k * sn;
		float horizontal = min(body.x * body.w * 0.12, 0.2) * a;
		w.displacement.xy += horizontal * cs * direction; w.velocity.xy += horizontal * omega * sn * direction;
		float jacobian = horizontal * k * sn;
		j00 -= jacobian * direction.x * direction.x; j01 -= jacobian * direction.x * direction.y;
		j11 -= jacobian * direction.y * direction.y;
	}
	float det = max(j00 * j11 - j01 * j01, 0.5);
	w.slope = vec2(j11 * w.slope.x - j01 * w.slope.y, j00 * w.slope.y - j01 * w.slope.x) / det;
	w.displacement.z = w.height;
	return w;
}

/*[Fragment]*/
// Keep the two stages byte-for-byte equivalent: the compact shader format
// deliberately has no textual include directive.
struct WaterSurfaceCommonState
{
	vec3 displacement;
	vec2 slope;
	vec3 velocity;
	float height;
	float attenuation;
	float curvature;
};

WaterSurfaceCommonState EvaluateWaterSurfaceCommon(vec3 worldPosition, float time,
	vec4 controls, vec4 profile, vec4 body, vec4 flow, vec4 flowDetail, vec4 terms[8])
{
	WaterSurfaceCommonState w;
	w.displacement = vec3(0.0); w.slope = vec2(0.0); w.velocity = vec3(0.0);
	w.height = 0.0; w.curvature = 0.0; w.attenuation = 1.0;
	if (controls.x < 0.5) return w;
	float speed = profile.z * controls.w * 16.0;
	if (flowDetail.z > 0.5 && body.z > 0.0) w.attenuation = smoothstep(0.0, 32.0, body.z);
	int count = body.y < 0.5 ? 2 : (body.y < 1.5 ? 4 : 8);
	float qualityAmplitude = count == 2 ? 1.86 : (count == 8 ? 0.59 : 1.0);
	float j00 = 1.0, j01 = 0.0, j11 = 1.0;
	for (int i = 0; i < 8; ++i)
	{
		if (i >= count) break;
		int component = count == 8 ? i : (i < count / 2 ? i : 4 + i - count / 2);
		float fi = float(component); vec4 term = terms[component]; vec2 direction = term.xy;
		float a = term.z * w.attenuation * qualityAmplitude; float k = term.w;
		float omega = speed * k; vec2 phasePosition = worldPosition.xy;
		if (flow.w > 0.5 && component >= 4) phasePosition -= flow.xy * time * flowDetail.y * 0.35;
		float phase = k * dot(direction, phasePosition) - omega * time + fi * 1.37;
		float sn = sin(phase), cs = cos(phase);
		w.height += a * sn; w.slope += a * k * cs * direction; w.velocity.z -= a * omega * cs;
		w.curvature -= a * k * k * sn;
		float horizontal = min(body.x * body.w * 0.12, 0.2) * a;
		w.displacement.xy += horizontal * cs * direction; w.velocity.xy += horizontal * omega * sn * direction;
		float jacobian = horizontal * k * sn;
		j00 -= jacobian * direction.x * direction.x; j01 -= jacobian * direction.x * direction.y;
		j11 -= jacobian * direction.y * direction.y;
	}
	float det = max(j00 * j11 - j01 * j01, 0.5);
	w.slope = vec2(j11 * w.slope.x - j01 * w.slope.y, j00 * w.slope.y - j01 * w.slope.x) / det;
	w.displacement.z = w.height;
	return w;
}
