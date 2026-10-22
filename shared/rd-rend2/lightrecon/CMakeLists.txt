# One CPU implementation, shared by SP, MP and standalone tools. No renderer
# defines, engine libraries, generated shaders or platform allocator dependency.
if(TARGET rend2_lightrecon_core)
	return()
endif()
add_library(rend2_lightrecon_core STATIC
	../tr_lightrecon_types.cpp ../tr_lightrecon_types.h ../tr_pbr_contract.h
	../tr_lightrecon_api.cpp ../tr_lightrecon_api.h ../tr_lightrecon_manager.h
	input.cpp input.h session.cpp session.h control.cpp control.h
	disk.cpp disk.h cache.cpp cache.h ../tr_lightrecon_cvars.inc)
set_target_properties(rend2_lightrecon_core PROPERTIES
	CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
	POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(rend2_lightrecon_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
