import json, math, sys
# mkviews.py out.cfg tag map "x y z" ... : views around each target, debug off / 2 / 3, portal debug 2
out, tag, m = sys.argv[1], sys.argv[2], sys.argv[3]
targets = [[float(v) for v in t.split()] for t in sys.argv[4:]]
lines = ['echo ==== MAP %s ====' % m, 'devmap %s' % m, 'wait 250', 'exitview', 'wait 60', 'notarget', 'god', 'noclip',
         'cg_draw2D 0', 'cg_thirdPerson 0', 'echo ==== STATS %s ====' % m,
         'r_vfogStaticStats proxies', 'r_vfogStaticStats lights', 'r_vfogStaticStats portals', 'wait 5']
for k, (x, y, z) in enumerate(targets):
    for yaw in (0, 90, 180, 270):
        cx = x - 320 * math.cos(math.radians(yaw)); cy = y - 320 * math.sin(math.radians(yaw))
        lines += ['setviewpos %.0f %.0f %.0f %d' % (cx, cy, z - 30, yaw), 'r_staticLightDebug 0', 'r_lightPortalDebug 0', 'wait 40',
                  'screenshot %s_%s_t%d_y%d_off' % (tag, m, k, yaw), 'r_staticLightDebug 2', 'r_lightPortalDebug 2', 'wait 5',
                  'screenshot %s_%s_t%d_y%d_dbg' % (tag, m, k, yaw)]
lines += ['r_staticLightDebug 0', 'r_lightPortalDebug 0']
open(out, 'a').write('\n'.join(lines) + '\n')
