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

// Keep a bounded list of slow assets rather than printing every registration.
struct mapLoadTopEntry_t {
	long long usec;
	char name[MAX_QPATH];
};

static inline void R_LoadProfileTopAdd(mapLoadTopEntry_t *top, int count,
	const char *name, long long usec)
{
	if (!name || usec <= 0) return;
	for (int i = 0; i < count; ++i) {
		if (usec <= top[i].usec) continue;
		for (int j = count - 1; j > i; --j) top[j] = top[j - 1];
		top[i].usec = usec;
		Q_strncpyz(top[i].name, name, sizeof(top[i].name));
		break;
	}
}
