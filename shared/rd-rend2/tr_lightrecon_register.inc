// Included inside R_Register with LIGHTRECON_GET set to the SP or MP API.
// Kept testable with CPU-only registration shims; no engine dependency here.
#define LIGHTRECON_CONTROL(name, maximum, description) \
	name = LIGHTRECON_GET(#name, "0", CVAR_ARCHIVE, description); \
	R_CvarRange(name, 0, maximum, qtrue);
#define LIGHTRECON_CONSUMER(name, kind, flags, description) \
	name = LIGHTRECON_GET(#name, "0", flags, description); \
	R_CvarRange(name, 0, 1, qtrue);
#include "tr_lightrecon_cvars.inc"
#undef LIGHTRECON_CONSUMER
#undef LIGHTRECON_CONTROL
