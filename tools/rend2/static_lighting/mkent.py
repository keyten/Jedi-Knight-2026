import sys
out = sys.argv[1]; spots = {}
for a in sys.argv[2:]:
    m, x, y, z = a.split(':'); spots.setdefault(m, []).append((float(x), float(y), float(z)))
configs = [('legacy', 'r_entityLightGrid 0; r_entityLightProbes 0; r_entityLightGridDebug 0'),
           ('grid2', 'r_entityLightGrid 2; r_entityLightProbes 0; r_entityLightGridDebug 0'),
           ('l1', 'r_entityLightGrid 2; r_entityLightProbes 1; r_entityLightGridDebug 0'),
           ('l1dbg10', 'r_entityLightGrid 2; r_entityLightProbes 1; r_entityLightGridDebug 10'),
           ('l1dbg11', 'r_entityLightGrid 2; r_entityLightProbes 1; r_entityLightGridDebug 11')]
L = []
for m, pts in spots.items():
    L += ['echo ==== MAP %s ====' % m, 'devmap %s' % m, 'wait 250', 'exitview', 'wait 60', 'notarget', 'god', 'cg_draw2D 0', 'con_notifytime 0']
    for tag, cv in configs:
        L += [cv, 'vid_restart', 'wait 200', 'echo ==== CONFIG %s %s ====' % (m, tag)]
        for k, (x, y, z) in enumerate(pts):
            for yaw in (0, 90, 180):
                L += ['setviewpos %.0f %.0f %.0f %d' % (x, y, z + 40, yaw), 'wait 60', 'screenshot ent_%s_s%d_%s_y%d' % (m, k, tag, yaw)]
L += ['quit']
open(out, 'w').write('\n'.join(L) + '\n')
