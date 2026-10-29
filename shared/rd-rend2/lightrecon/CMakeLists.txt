# One CPU implementation, shared by SP, MP and standalone tools. No renderer
# defines, engine libraries, generated shaders or platform allocator dependency.
if(TARGET rend2_lightrecon_core)
	return()
endif()
add_library(rend2_lightrecon_core STATIC
	../tr_lightrecon_types.cpp ../tr_lightrecon_types.h ../tr_pbr_contract.h
	../tr_lightrecon_api.cpp ../tr_lightrecon_api.h ../tr_lightrecon_manager.h
	input.cpp input.h session.cpp session.h control.cpp control.h lightmaps.cpp lightmaps.h
	decode.cpp decode.h
	bake_profile.h bake_forward.cpp bake_forward.h bake_math.h bake_trace.cpp bake_trace.h bake_integral.cpp
	sun_fit.cpp sun_fit.h
	hidden_candidates.cpp hidden_candidates.h
	candidate_pool.cpp candidate_pool.h
	amplitude_fit.cpp amplitude_fit.h
	point_refine.cpp point_refine.h
	emitter_shape.cpp emitter_shape.h
	fit_evidence.h shadow_evidence.cpp shadow_evidence.h
	lightgrid.cpp lightgrid.h
	surfaces.cpp surfaces.h
	classification.cpp classification.h
	visibility.cpp visibility.h
	regions.cpp regions.h geometry_key.h
	emission_geometry.cpp emission_geometry.h evidence.cpp evidence.h
	offline.cpp offline.h offline_io.cpp offline_bsp.cpp
	disk.cpp disk.h cache.cpp cache.h lifecycle.cpp lifecycle.h ../tr_lightrecon_cvars.inc)
set_target_properties(rend2_lightrecon_core PROPERTIES
	CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
	POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(rend2_lightrecon_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
