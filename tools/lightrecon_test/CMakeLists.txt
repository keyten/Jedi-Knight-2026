cmake_minimum_required(VERSION 3.1...3.31)
if(NOT InOpenJK)
	project(LightReconContractTests LANGUAGES CXX)
	enable_testing()
endif()

add_executable(lightrecon_test
	lightrecon_test.cpp
	../../shared/rd-rend2/tr_lightrecon_types.cpp)
set_target_properties(lightrecon_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(MSVC)
	target_compile_options(lightrecon_test PRIVATE /W4)
else()
	target_compile_options(lightrecon_test PRIVATE -Wall -Wextra -pedantic)
endif()
add_test(NAME lightrecon_contract COMMAND lightrecon_test)

add_executable(lightrecon_api_test
	lightrecon_api_test.cpp
	../../shared/rd-rend2/tr_lightrecon_api.cpp
	../../shared/rd-rend2/tr_lightrecon_types.cpp)
set_target_properties(lightrecon_api_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(MSVC)
	target_compile_options(lightrecon_api_test PRIVATE /W4)
else()
	target_compile_options(lightrecon_api_test PRIVATE -Wall -Wextra -pedantic)
endif()
add_test(NAME lightrecon_api COMMAND lightrecon_api_test)
