/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Renderer cvar registry: descriptions (also in SP, whose engine has none),
// value checks and help.
//
// Every renderer cvar is registered through R_Cvar, its range through
// R_CvarRange. The renderer, not the engine, validates the values: once per
// frame (RE_BeginFrame), at map load and after registration. A value that is
// not a number, not a whole number, out of range, the wrong number of vector
// components or an unknown mode is rejected: the previous value is restored
// and the cvar's help is printed (what it does, usage, default, example), the
// way r_vfog prints its usage. r_help <cvar> prints the same help on demand.

#include "tr_local.h"

#include <algorithm>

namespace
{
enum cvarHelpKind_t
{
	CVHK_STRING,		// anything (paths, names, lists)
	CVHK_NUMBER,		// a number
	CVHK_INTEGER,		// a whole number
	CVHK_AUTO,			// "auto" or a number
	CVHK_VECTOR,		// 2..4 numbers separated by spaces
};

struct cvarHelp_t
{
	cvar_t *cvar;
	std::string description;	// a copy: callers may pass va() text
	const char *text;			// R_CvarHelpText: modes, notes and examples
	cvarHelpKind_t kind;
	int components;
	qboolean ranged;
	float minValue;
	float maxValue;
	int modificationCount;
	std::string lastValid;
};

std::vector<cvarHelp_t> s_cvarHelp;
std::unordered_map<std::string, size_t> s_cvarHelpIndex;

std::string R_CvarHelpKey( const char *name )
{
	std::string key(name);
	for ( char &c : key )
		c = (char)tolower((unsigned char)c);
	return key;
}

qboolean R_CvarHelpParseNumber( const char *s, float *out )
{
	while ( *s == ' ' || *s == '\t' )
		s++;
	if ( !*s )
		return qfalse;

	char *end;
	const double value = strtod(s, &end);
	if ( end == s )
		return qfalse;
	while ( *end == ' ' || *end == '\t' )
		end++;
	if ( *end != '\0' || value != value || value > 3.0e38 || value < -3.0e38 )
		return qfalse;

	if ( out )
		*out = (float)value;
	return qtrue;
}

// numbers separated by spaces; -1 when one of them is not a number
int R_CvarHelpCountNumbers( const char *s )
{
	int count = 0;
	char token[64];
	while ( *s )
	{
		while ( *s == ' ' || *s == '\t' )
			s++;
		if ( !*s )
			break;

		size_t length = 0;
		while ( s[length] && s[length] != ' ' && s[length] != '\t' )
			length++;
		if ( length >= sizeof(token) )
			return -1;
		Com_Memcpy(token, s, length);
		token[length] = '\0';
		if ( !R_CvarHelpParseNumber(token, NULL) )
			return -1;
		count++;
		s += length;
	}
	return count;
}

void R_CvarHelpInferKind( cvarHelp_t& h, const char *defaultValue )
{
	h.components = 1;
	if ( !Q_stricmp(defaultValue, "auto") )
		h.kind = CVHK_AUTO;
	else if ( R_CvarHelpParseNumber(defaultValue, NULL) )
		h.kind = CVHK_NUMBER;
	else
	{
		const int count = R_CvarHelpCountNumbers(defaultValue);
		if ( count >= 2 && count <= 4 )
		{
			h.kind = CVHK_VECTOR;
			h.components = count;
		}
		else
			h.kind = CVHK_STRING;
	}
}

const char *R_CvarHelpPending( const cvar_t *cv )
{
	return cv->latchedString ? cv->latchedString : cv->string;
}

// why this value is not accepted, NULL when it is
const char *R_CvarHelpReject( const cvarHelp_t& h, const char *value )
{
	float number = 0.0f;
	switch ( h.kind )
	{
	case CVHK_STRING:
		return NULL;

	case CVHK_VECTOR:
	{
		const int count = R_CvarHelpCountNumbers(value);
		if ( count < 0 )
			return "needs numbers";
		if ( count != h.components )
			return va("needs %i numbers", h.components);
		return NULL;
	}

	case CVHK_AUTO:
		if ( !Q_stricmp(value, "auto") )
			return NULL;
		if ( !R_CvarHelpParseNumber(value, &number) )
			return "needs a number or auto";
		break;

	case CVHK_NUMBER:
	case CVHK_INTEGER:
		if ( !R_CvarHelpParseNumber(value, &number) )
			return "needs a number";
		break;
	}

	if ( h.kind == CVHK_INTEGER && number != floorf(number) )
		return "needs a whole number";
	if ( h.ranged && (number < h.minValue || number > h.maxValue) )
		return "is out of range";
	return NULL;
}

// a value worth trying that differs from the default
void R_CvarHelpExample( const cvarHelp_t& h, char *out, size_t size )
{
	const char *def = h.cvar->resetString ? h.cvar->resetString : "";
	float value = 0.0f;
	switch ( h.kind )
	{
	case CVHK_STRING:
	case CVHK_VECTOR:
		Com_sprintf(out, size, "\"%s\"", def);
		return;
	case CVHK_AUTO:
		if ( Q_stricmp(def, "auto") )
			Q_strncpyz(out, "auto", size);
		else
			Com_sprintf(out, size, "%g", h.ranged ? Com_Clamp(h.minValue, h.maxValue, 0.0f) : 0.0f);
		return;
	case CVHK_INTEGER:
	{
		const int defInt = atoi(def);
		int example = defInt + 1;
		if ( h.ranged && example > (int)h.maxValue )
			example = defInt - 1;
		if ( h.ranged && example < (int)h.minValue )
			example = (int)h.minValue;
		Com_sprintf(out, size, "%i", example);
		return;
	}
	case CVHK_NUMBER:
		R_CvarHelpParseNumber(def, &value);
		if ( value != 0.0f )
			value *= 2.0f;
		else
			value = h.ranged && h.maxValue < 1.0f ? h.maxValue : 1.0f;
		if ( h.ranged )
		{
			if ( value > h.maxValue )
				value = 0.5f * (h.minValue + h.maxValue);
			value = Com_Clamp(h.minValue, h.maxValue, value);
		}
		Com_sprintf(out, size, "%g", value);
		return;
	}
	Q_strncpyz(out, def, size);
}

void R_CvarHelpPrintEntry( const cvarHelp_t& h )
{
	const cvar_t *cv = h.cvar;
	if ( !h.description.empty() )
		ri.Printf(PRINT_ALL, "  %s\n", h.description.c_str());

	char usage[64];
	switch ( h.kind )
	{
	case CVHK_STRING:
		Q_strncpyz(usage, "<text>", sizeof(usage));
		break;
	case CVHK_VECTOR:
		Com_sprintf(usage, sizeof(usage), "\"<%i numbers>\"", h.components);
		break;
	case CVHK_AUTO:
	case CVHK_NUMBER:
		if ( h.ranged )
			Com_sprintf(usage, sizeof(usage), "%s<%g..%g>", h.kind == CVHK_AUTO ? "auto | " : "", h.minValue, h.maxValue);
		else
			Q_strncpyz(usage, h.kind == CVHK_AUTO ? "auto | <number>" : "<number>", sizeof(usage));
		break;
	case CVHK_INTEGER:
	default:
		if ( h.ranged )
			Com_sprintf(usage, sizeof(usage), "<%i..%i>", (int)h.minValue, (int)h.maxValue);
		else
			Q_strncpyz(usage, "<whole number>", sizeof(usage));
		break;
	}

	char notes[128];
	Com_sprintf(notes, sizeof(notes), "default \"%s\"", cv->resetString ? cv->resetString : "");
	if ( cv->flags & CVAR_LATCH )
		Q_strcat(notes, sizeof(notes), ", applied at vid_restart");
	if ( cv->flags & CVAR_CHEAT )
		Q_strcat(notes, sizeof(notes), ", needs cheats");
	if ( cv->flags & CVAR_ARCHIVE )
		Q_strcat(notes, sizeof(notes), ", saved");
	ri.Printf(PRINT_ALL, "  usage:   %s %s   (%s)\n", cv->name, usage, notes);
	if ( h.text )
		ri.Printf(PRINT_ALL, "%s", h.text);
	else
	{
		char example[MAX_CVAR_VALUE_STRING];
		R_CvarHelpExample(h, example, sizeof(example));
		ri.Printf(PRINT_ALL, "  example: %s %s\n", cv->name, example);
	}
}

// a registered value that is no longer valid: restore, explain
void R_CvarHelpRestore( cvarHelp_t& h, const char *value, const char *reason )
{
	const char *restore = h.lastValid.empty() ?
		(h.cvar->resetString ? h.cvar->resetString : "") : h.lastValid.c_str();
	std::string restored(restore);
	ri.Printf(PRINT_WARNING, "%s \"%s\" %s, kept \"%s\".\n", h.cvar->name, value, reason, restored.c_str());
	R_CvarHelpPrintEntry(h);
	ri.Cvar_Set(h.cvar->name, restored.c_str());
	h.lastValid = restored;
	h.modificationCount = h.cvar->modificationCount;
}

void R_CvarHelpCheck( cvarHelp_t& h )
{
	cvar_t *cv = h.cvar;
	if ( cv->modificationCount == h.modificationCount )
		return;
	h.modificationCount = cv->modificationCount;
	if ( cv->flags & (CVAR_ROM | CVAR_INIT) )
		return;

	const char *value = R_CvarHelpPending(cv);
	const char *reason = R_CvarHelpReject(h, value);
	if ( !reason )
	{
		h.lastValid = value;
		return;
	}

	std::string rejected(value);
	R_CvarHelpRestore(h, rejected.c_str(), reason);
}

cvarHelp_t *R_CvarHelpFind( const char *name )
{
	auto it = s_cvarHelpIndex.find(R_CvarHelpKey(name));
	return it == s_cvarHelpIndex.end() ? NULL : &s_cvarHelp[it->second];
}
} // namespace

cvar_t *R_Cvar( const char *name, const char *value, int flags, const char *description )
{
#ifdef REND2_SP
	cvar_t *cv = ri.Cvar_Get(name, value, flags);
#else
	cvar_t *cv = ri.Cvar_Get(name, value, (uint32_t)flags, description ? description : "");
#endif
	if ( !cv )
		return cv;

	const std::string key = R_CvarHelpKey(name);
	auto it = s_cvarHelpIndex.find(key);
	cvarHelp_t *h;
	if ( it == s_cvarHelpIndex.end() )
	{
		s_cvarHelpIndex[key] = s_cvarHelp.size();
		s_cvarHelp.emplace_back();
		h = &s_cvarHelp.back();
		h->text = NULL;
	}
	else
		h = &s_cvarHelp[it->second];

	h->cvar = cv;
	h->description = description ? description : "";
	h->ranged = qfalse;
	h->minValue = h->maxValue = 0.0f;
	R_CvarHelpInferKind(*h, cv->resetString ? cv->resetString : value);

	// the stored value (config, command line) is checked at R_CvarRange or
	// R_CvarValidateAll, when the range is known
	h->modificationCount = cv->modificationCount - 1;
	h->lastValid.clear();
	return cv;
}

void R_CvarRange( cvar_t *cv, float minValue, float maxValue, qboolean integral )
{
	if ( !cv )
		return;

	cvarHelp_t *h = R_CvarHelpFind(cv->name);
	if ( !h )
	{
		// a cvar of another module: let the engine check it
		ri.Cvar_CheckRange(cv, minValue, maxValue, integral);
		return;
	}

	h->ranged = qtrue;
	h->minValue = minValue;
	h->maxValue = maxValue;
	if ( h->kind == CVHK_NUMBER && integral )
		h->kind = CVHK_INTEGER;
	R_CvarHelpCheck(*h);
}

// for the lazy getters outside R_Register
cvar_t *R_CvarInt( const char *name, const char *value, int flags, int minValue, int maxValue, const char *description )
{
	cvar_t *cv = R_Cvar(name, value, flags, description);
	R_CvarRange(cv, (float)minValue, (float)maxValue, qtrue);
	return cv;
}

cvar_t *R_CvarFind( const char *name )
{
	cvarHelp_t *h = R_CvarHelpFind(name);
	return h ? h->cvar : NULL;
}

void R_CvarHelpText( cvar_t *cv, const char *text )
{
	cvarHelp_t *h = cv ? R_CvarHelpFind(cv->name) : NULL;
	if ( h )
		h->text = text;
}

void R_CvarValidateAll( void )
{
	for ( cvarHelp_t& h : s_cvarHelp )
		R_CvarHelpCheck(h);
}

qboolean R_CvarHelpPrint( const char *name )
{
	cvarHelp_t *h = R_CvarHelpFind(name);
	if ( !h )
		return qfalse;

	ri.Printf(PRINT_ALL, "%s is \"%s\"", h->cvar->name, h->cvar->string);
	if ( h->cvar->latchedString )
		ri.Printf(PRINT_ALL, " (\"%s\" after vid_restart)", h->cvar->latchedString);
	ri.Printf(PRINT_ALL, "\n");
	R_CvarHelpPrintEntry(*h);
	return qtrue;
}

/*
=================
R_CvarHelp_f

r_help <cvar>: description, usage, default and an example.
r_help <text>: every renderer cvar whose name contains the text.
=================
*/
void R_CvarHelp_f( void )
{
	if ( ri.Cmd_Argc() < 2 )
	{
		ri.Printf(PRINT_ALL,
			"usage: r_help <cvar>     what a renderer cvar does, its values and an example\n"
			"       r_help <text>     list the renderer cvars whose name contains the text\n"
			"example: r_help r_ssrCompare, r_help volumetricFog\n");
		return;
	}

	const char *name = ri.Cmd_Argv(1);
	if ( R_CvarHelpPrint(name) )
		return;

	const std::string needle = R_CvarHelpKey(name);
	std::vector<const cvarHelp_t *> matches;
	for ( const cvarHelp_t& h : s_cvarHelp )
	{
		if ( R_CvarHelpKey(h.cvar->name).find(needle) != std::string::npos )
			matches.push_back(&h);
	}
	if ( matches.empty() )
	{
		ri.Printf(PRINT_ALL, "r_help: no renderer cvar contains \"%s\"\n", name);
		return;
	}

	std::sort(matches.begin(), matches.end(), []( const cvarHelp_t *a, const cvarHelp_t *b ) {
		return Q_stricmp(a->cvar->name, b->cvar->name) < 0;
	});
	for ( const cvarHelp_t *h : matches )
		ri.Printf(PRINT_ALL, "%-36s %s\n", h->cvar->name, h->description.c_str());
	ri.Printf(PRINT_ALL, "%i cvars; r_help <cvar> for one of them\n", (int)matches.size());
}
