#pragma once

// CPU wall time only. GPU work can complete later; do not interpret these
// numbers as GPU timings or add nested scopes to obtain a total.
static inline bool R_LoadProfileEnabled()
{
	return ri.Cvar_VariableIntegerValue("r_loadProfile") != 0;
}

static inline void R_LoadProfilePrint(const char *stage, int start)
{
	if (R_LoadProfileEnabled())
		ri.Printf(PRINT_ALL, "[map load] %-28s %6d ms\n", stage,
			ri.Milliseconds() - start);
}
