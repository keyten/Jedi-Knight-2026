# rend2_glslc: links GLSL programs for rend2 in helper processes
# (shared/rd-rend2/glslc, r_shaderCompileProcesses). Installed next to the
# renderer DLLs, where tr_glslhelper.cpp looks for it. SP and MP share it.
function(add_rend2_glslc InstallDir Component)
	if(NOT WIN32 OR TARGET rend2_glslc)
		return()
	endif()
	add_executable(rend2_glslc
		${SharedDir}/rd-rend2/glslc/rend2_glslc.cpp
		${SharedDir}/rd-rend2/glslc/glslc_protocol.h)
	target_compile_definitions(rend2_glslc PRIVATE "NOMINMAX")
	target_link_renderer_sdl2(rend2_glslc)
	install(TARGETS rend2_glslc
		RUNTIME
		DESTINATION ${InstallDir}
		COMPONENT ${Component})
endfunction()
