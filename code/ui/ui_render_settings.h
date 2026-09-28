// SP rendering overlay helpers. Included only by ui_main.cpp.
#include <cmath>
#include <string>

struct renderSetting_t {
	std::string shadow, baseline;
	cvar_t *real;
	bool restart, integer, numeric;
};
static std::vector<renderSetting_t> renderSettings;
static std::vector<std::string> renderLuts;
static bool renderSettingsInitialized = false;

static void UI_RenderLutsRefresh() {
	char files[32768];
	const int count = ui.FS_GetFileList("luts", ".cube", files, sizeof(files));
	renderLuts.clear();
	const char *file = files;
	for (int i = 0; i < count; ++i, file += strlen(file) + 1) {
		// Filenames are data, never console commands. Limit to valid virtual paths.
		if (!strchr(file, '/') && !strchr(file, '\\') && strlen(file) + 6 < MAX_QPATH)
			renderLuts.push_back(std::string("luts/") + file);
	}
	std::sort(renderLuts.begin(), renderLuts.end());
	renderLuts.erase(std::unique(renderLuts.begin(), renderLuts.end()), renderLuts.end());
	menuDef_t *menu = Menus_FindByName("render2026_luts");
	if (menu) {
		const char *selected = Cvar_VariableString("r_colorGradingLUT");
		int row = !*selected ? 0 : !Q_stricmp(selected, "*identity") ? 1 : -1;
		for (size_t i = 0; i < renderLuts.size(); ++i)
			if (!Q_stricmp(selected, renderLuts[i].c_str())) row = static_cast<int>(i) + 2;
		for (int i = 0; i < menu->itemCount; ++i) {
			itemDef_t *item = menu->items[i];
			if (item->type != ITEM_TYPE_LISTBOX || item->special != FEEDER_RENDER_LUTS) continue;
			listBoxDef_t *list = static_cast<listBoxDef_t *>(item->typeData);
			item->cursorPos = list->cursorPos = row;
			list->startPos = row > 9 ? row - 9 : 0;
		}
	}
}

static const char *UI_RenderSettingValue(const renderSetting_t &setting) {
	return setting.real->latchedString ? setting.real->latchedString : setting.real->string;
}

static void UI_RenderSettingsUpdate() {
	bool dirty = false;
	for (const auto &setting : renderSettings) {
		const char *value = UI_RenderSettingValue(setting);
		Cvar_Set(setting.shadow.c_str(), value);
		const bool changed = setting.numeric ? atof(value) != atof(setting.baseline.c_str())
		                                    : setting.baseline != value;
		dirty |= setting.restart && changed;
	}
	Cvar_Set("ui_r2026_restartPending", dirty ? "1" : "0");
}

static void UI_RenderSettingsBegin() {
	if (!renderSettingsInitialized) {
		renderSettings.clear();
		for (int p = 0; p < MAX_MENUS; ++p) {
			menuDef_t *menu = Menus_FindByName(va("render2026_%d", p));
			if (!menu) continue;
			for (int i = 0; i < menu->itemCount; ++i) {
				const itemDef_t *item = menu->items[i];
				if (!item->cvar || Q_strncmp(item->cvar, "ui_r2026_r_", 11)) continue;
				// Registered renderer cvars already exist; preserve their flags/defaults.
				const char *name = item->cvar + strlen("ui_r2026_");
				cvar_t *real = Cvar_Get(name, Cvar_VariableString(name), 0);
				const char *group = item->window.group ? item->window.group : "";
				const bool numericMulti = item->type == ITEM_TYPE_MULTI &&
					!static_cast<multiDef_t *>(item->typeData)->strDef;
				renderSettings.push_back({item->cvar, real->string, real,
					strstr(group, "restart") != nullptr, strstr(group, "integer") != nullptr || numericMulti,
					item->type == ITEM_TYPE_SLIDER || numericMulti});
			}
		}
		renderSettingsInitialized = true;
		UI_RenderLutsRefresh();
	}
	UI_RenderSettingsUpdate();
}

static void UI_RenderSettingsSetCvar(const char *name, const char *value) {
	for (const auto &setting : renderSettings) {
		if (setting.shadow != name) continue;
		std::string normalized = value;
		if (setting.integer) normalized = va("%.0f", floor(atof(value) + 0.5));
		// Respect CVAR_LATCH: allocating renderer buffers requires vid_restart.
		Cvar_Set2(setting.real->name, normalized.c_str(), qfalse);
		UI_RenderSettingsUpdate();
		return;
	}
	Cvar_Set(name, value);
}

static void UI_RenderLutSelect(int index) {
	if (index < 0 || index >= static_cast<int>(renderLuts.size()) + 2) return;
	const char *value = index == 0 ? "" : index == 1 ? "*identity" : renderLuts[index - 2].c_str();
	UI_RenderSettingsSetCvar("ui_r2026_r_colorGradingLUT", value);
}
