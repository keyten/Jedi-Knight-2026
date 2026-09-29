@echo off
rem Install next to openjk_sp.x86_64.exe. New flags are explicit here only.
cd /d "%~dp0"
start "" "openjk_sp.x86_64.exe" +set fs_basepath "%~dp0." +set fs_game OpenJK +set cl_renderer rdsp-rend2 +set r_volumetricFog 2 +set r_volumetricParticles 1 +set fx_physicalization 1 +set fx_physicalizationComposite 1 +set fx_physicalizationAdaptive 1 +set fx_physicalizationSources 0 +set developer 1
