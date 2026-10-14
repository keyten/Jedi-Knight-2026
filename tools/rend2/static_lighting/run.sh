#!/bin/sh
# usage: run.sh sp|mp <cfgname> [extra +set args...]
T="${JKA_TEST_DIR:?set JKA_TEST_DIR to the test install (exe, renderer DLLs, home/, hide.ps1)}"
kind=$1; cfg=$2; shift 2
BASE="${JKA_BASEPATH:?set JKA_BASEPATH to the game install with base/*.pk3}"
COMMON="+set fs_basepath $BASE +set fs_homepath $T/home +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set developer 1 +set logfile 2 +set r_loadProfile 1 +set com_cameraMode 0 +set s_initsound 0 \
 +set r_forwardPlus 1 +set r_volumetricFog 2 +set r_staticLightReconstruction 1 +set r_volumetricFogStaticDirectional 1 +set r_recoveredVolumetricLights 1 +set r_lightPortals 1 +set r_entityLightGrid 2 +set r_entityLightProbes 1"
cd "$T"
# keep the game window hidden (watcher hides every window of the process as it appears)
powershell -NoProfile -ExecutionPolicy Bypass -File "$T/hide.ps1" -Mode hide &
if [ "$kind" = sp ]; then
  exec ./openjk_sp.x86_64.exe +set fs_game OpenJK +set cl_renderer rdsp-rend2 $COMMON "$@" +exec $cfg
else
  exec ./openjk.x86_64.exe +set cl_renderer rd-rend2 +set sv_pure 0 $COMMON "$@" +exec $cfg
fi
