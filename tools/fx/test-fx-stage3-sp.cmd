@echo off
rem Explicit experimental launcher. Normal defaults remain off.
cd /d "%~dp0"
start "" "openjk_sp.x86_64.exe" +set fs_basepath "%~dp0." +set fs_game OpenJK +set cl_renderer rdsp-rend2 +set r_volumetricFog 2 +set r_volumetricParticles 1 +set fx_physicalization 1 +set fx_physicalizationComposite 1 +set fx_physicalizationAdaptive 1 +set fx_physicalizationEmission 1 +set fx_physicalizationAggregate 0 +set fx_physicalizationSources 0 +set developer 1
