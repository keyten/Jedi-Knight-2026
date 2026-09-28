// Compile with a C++11 compiler. Executes the production overlay state helpers
// against a minimal cvar/filesystem fixture, without launching the renderer.
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

constexpr int MAX_QPATH=64, MAX_MENUS=64, FEEDER_RENDER_LUTS=112;
constexpr int ITEM_TYPE_SLIDER=10, ITEM_TYPE_MULTI=12, ITEM_TYPE_LISTBOX=6;
constexpr int qfalse=0;
struct cvar_t {
    std::string key, active, pending;
    const char *name=nullptr, *string=nullptr, *latchedString=nullptr;
    bool latch=false;
    void sync() { name=key.c_str(); string=active.c_str(); latchedString=pending.empty()?nullptr:pending.c_str(); }
};
std::map<std::string,cvar_t> cvars;
void Cvar_Set(const char *name,const char *value) {
    auto &c=cvars[name]; c.key=name; c.active=value; c.pending.clear(); c.sync();
}
void Cvar_Set2(const char *name,const char *value,int force) {
    auto &c=cvars[name]; c.key=name;
    if(c.latch && !force) c.pending=c.active==value?"":value;
    else {c.active=value;c.pending.clear();}
    c.sync();
}
const char *Cvar_VariableString(const char *name) {return cvars[name].active.c_str();}
cvar_t *Cvar_Get(const char *name,const char *value,int) {
    if(!cvars.count(name)) Cvar_Set(name,value);
    return &cvars[name];
}
int Q_strncmp(const char *a,const char *b,int n){return strncmp(a,b,n);}
int Q_stricmp(const char *a,const char *b){return strcmp(a,b);}
const char *va(const char *fmt,...) {
    static char b[2048]; va_list args;va_start(args,fmt);vsnprintf(b,sizeof(b),fmt,args);va_end(args);return b;
}
struct window_t {const char *group;};
struct multiDef_t {bool strDef;};
struct listBoxDef_t {int cursorPos,startPos;};
struct itemDef_t {const char *cvar;window_t window;int type;void *typeData;float special;int cursorPos;};
struct menuDef_t {int itemCount;itemDef_t **items;};
menuDef_t *fixtureMenu=nullptr;
menuDef_t *Menus_FindByName(const char *name) {return !strcmp(name,"render2026_1")?fixtureMenu:nullptr;}
int FixtureFiles(const char *,const char *,char *buf,int) {
    static const char files[]="warm.cube\0cool.cube\0warm.cube\0bad/name.cube\0";
    memcpy(buf,files,sizeof(files));return 4;
}
struct {int(*FS_GetFileList)(const char*,const char*,char*,int);} ui={FixtureFiles};
#include "../../code/ui/ui_render_settings.h"

int main() {
    Cvar_Set("r_hdr","1");cvars["r_hdr"].latch=true;
    Cvar_Set("r_samples","2");
    Cvar_Set("r_brightness","1");
    Cvar_Set("r_colorGradingLUT","");
    multiDef_t toggle={false};
    itemDef_t fields[]={
        {"ui_r2026_r_hdr",{"render2026_restart"},ITEM_TYPE_MULTI,&toggle,0,0},
        {"ui_r2026_r_samples",{"render2026_integer"},ITEM_TYPE_SLIDER,nullptr,0,0},
        {"ui_r2026_r_brightness",{"render2026"},ITEM_TYPE_SLIDER,nullptr,0,0},
        {"ui_r2026_r_colorGradingLUT",{"render2026"},1,nullptr,0,0}
    };
    itemDef_t *items[]={&fields[0],&fields[1],&fields[2],&fields[3]};
    menuDef_t menu={4,items};fixtureMenu=&menu;
    UI_RenderSettingsBegin();
    assert(renderSettings.size()==4);
    assert(cvars["ui_r2026_restartPending"].active=="0");
    UI_RenderSettingsSetCvar("ui_r2026_r_hdr","0.000000");
    assert(cvars["r_hdr"].active=="1" && cvars["r_hdr"].pending=="0");
    assert(cvars["ui_r2026_r_hdr"].active=="0");
    assert(cvars["ui_r2026_restartPending"].active=="1");
    UI_RenderSettingsSetCvar("ui_r2026_r_hdr","1.000000");
    assert(cvars["r_hdr"].pending.empty());
    assert(cvars["ui_r2026_restartPending"].active=="0");
    UI_RenderSettingsSetCvar("ui_r2026_r_samples","3.7");
    assert(cvars["r_samples"].active=="4");
    UI_RenderSettingsSetCvar("ui_r2026_r_brightness","0.5");
    assert(cvars["r_brightness"].active=="0.5");
    assert(cvars["ui_r2026_restartPending"].active=="0");
    // Restart markers on nonlatched settings must also detect exact reverts.
    renderSettings[2].restart=true;
    UI_RenderSettingsUpdate();assert(cvars["ui_r2026_restartPending"].active=="1");
    UI_RenderSettingsSetCvar("ui_r2026_r_brightness","1.000000");
    assert(cvars["ui_r2026_restartPending"].active=="0");
    UI_RenderLutsRefresh();
    assert(renderLuts.size()==2 && renderLuts[0]=="luts/cool.cube");
    UI_RenderLutSelect(2);assert(cvars["r_colorGradingLUT"].active=="luts/cool.cube");
    UI_RenderLutSelect(1);assert(cvars["r_colorGradingLUT"].active=="*identity");
    UI_RenderLutSelect(0);assert(cvars["r_colorGradingLUT"].active.empty());
    UI_RenderLutSelect(500);assert(cvars["r_colorGradingLUT"].active.empty());
    // UI reinitialization after restart must establish a new active baseline.
    Cvar_Set("r_hdr","0");
    renderSettings.clear();renderSettingsInitialized=false;
    UI_RenderSettingsBegin();
    assert(cvars["ui_r2026_r_hdr"].active=="0");
    assert(cvars["ui_r2026_restartPending"].active=="0");
    puts("PASS: live edits, pending latch display, restart dirty/revert, integer rounding, LUT scan/select");
}
