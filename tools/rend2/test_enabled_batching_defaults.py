"""Contract check for the explicitly selected SP/MP batching defaults and fallback controls."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
for game in ('code','codemp'):
    source=(ROOT/game/'rd-rend2/tr_init.cpp').read_text()
    for name,value in [('r_worldSurfaceList',1),('r_modelOcclusion',1),('r_surfaceSpritesCompute',2)]:
        assert re.search(r'R_Cvar\("'+name+r'", "'+str(value)+r'", CVAR_ARCHIVE',source),(game,name)
    assert 'R_CvarRange(r_surfaceSpritesCompute, 0, 2, qtrue)' in source
source=(ROOT/'shared/rd-rend2/tr_md3_instance.cpp').read_text()
assert 'R_CvarInt("r_md3Instancing", "1", CVAR_ARCHIVE, 0, 1' in source
assert '!Enabled()' in source
sprite=(ROOT/'shared/rd-rend2/tr_spritecull.cpp').read_text()
assert '!r_surfaceSpritesCompute->integer' in sprite
print('PASS SP/MP enabled batching defaults, archived settings and off controls')
