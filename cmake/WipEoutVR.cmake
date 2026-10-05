# The Steam Frame VR port (docs/VR_ARCHITECTURE.md). Included from the root
# CMakeLists.txt only when WIPEOUT_VR is ON. Adds -DWIPEOUT_VR=1, the XR host in
# src/vr/, the vendored sfvr library, a statically linked OpenXR loader and the
# unit tests (CTest).

if(NOT "${PLATFORM}" STREQUAL "SDL2" OR NOT "${RENDERER}" STREQUAL "GL")
	message(FATAL_ERROR "WIPEOUT_VR needs PLATFORM=SDL2 and RENDERER=GL")
endif()
if(CMAKE_VERSION VERSION_LESS 3.14)
	message(FATAL_ERROR "WIPEOUT_VR needs CMake 3.14 or newer (FetchContent_MakeAvailable)")
endif()

include(FetchContent)
enable_testing()


# OpenXR loader: OpenXR-SDK release-1.1.63 (commit f2448a8797c85814aa892efc1ab8707900fbcc78),
# fetched at configure time and checked against the archive's SHA-256. Linked
# statically, so the package ships no loader library.
set(WIPEOUT_VR_OPENXR_URL
	"https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.63.tar.gz"
	CACHE STRING "Where to fetch the OpenXR-SDK release-1.1.63 archive (a local path works too; the hash is always checked)")
set(WIPEOUT_VR_OPENXR_SHA256
	"5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f")

# CMP0077: plain variables override the SDK's option() defaults without
# touching the cache. They are unset again so they cannot leak.
set(wipeout_vr_xr_options
	DYNAMIC_LOADER BUILD_API_LAYERS BUILD_TESTS BUILD_CONFORMANCE_TESTS
	BUILD_WITH_SYSTEM_JSONCPP BUILD_WITH_XCB_HEADERS BUILD_WITH_WAYLAND_HEADERS)
foreach(option ${wipeout_vr_xr_options})
	set(${option} OFF)
endforeach()
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
if(NOT CMAKE_VERSION VERSION_LESS 3.24)
	set(wipeout_vr_extract_timestamp DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
FetchContent_Declare(wipeout_openxr
	URL "${WIPEOUT_VR_OPENXR_URL}"
	URL_HASH "SHA256=${WIPEOUT_VR_OPENXR_SHA256}"
	${wipeout_vr_extract_timestamp}
)
FetchContent_MakeAvailable(wipeout_openxr)
foreach(option ${wipeout_vr_xr_options})
	unset(${option})
endforeach()
unset(CMAKE_POLICY_DEFAULT_CMP0077)


# sfvr: vendored (third_party/sfvr/VENDORED.md). Its tests run through CTest.
set(SFVR_BUILD_TESTS ON CACHE BOOL "Build the sfvr tests" FORCE)
add_subdirectory(third_party/sfvr)


# The XR host without SDL or GL. The unit tests link this, with a fake runtime
# in place of the loader.
add_library(wipeout_vr_core STATIC
	src/vr/vr_clock.c
	src/vr/vr_clock.h
	src/vr/vr_cmd.c
	src/vr/vr_cmd.h
	src/vr/vr_cockpit_core.c
	src/vr/vr_cockpit_core.h
	src/vr/vr_cockpit_geometry.inc
	src/vr/vr_compose.c
	src/vr/vr_compose.h
	src/vr/vr_input.c
	src/vr/vr_input.h
	src/vr/vr_log.c
	src/vr/vr_log.h
	src/vr/vr_math.c
	src/vr/vr_math.h
	src/vr/vr_paths.c
	src/vr/vr_paths.h
	src/vr/vr_perf.c
	src/vr/vr_perf.h
	src/vr/vr_refresh.c
	src/vr/vr_refresh.h
	src/vr/vr_settings.c
	src/vr/vr_settings.h
	src/vr/vr_stage.c
	src/vr/vr_stage.h
	src/vr/vr_types.h
	src/vr/vr_xr.c
	src/vr/vr_xr.h
)
set_property(TARGET wipeout_vr_core PROPERTY C_STANDARD 11)
target_include_directories(wipeout_vr_core PUBLIC src/vr)
# vr_input.c uses the game's input and action headers, which have a WIPEOUT_VR fence
target_compile_definitions(wipeout_vr_core PUBLIC WIPEOUT_VR=1)
target_compile_options(wipeout_vr_core PRIVATE
	$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra>
)
# -ffp-contract=off (lesson 15) for the cockpit's trim rule only: it bins hues, and a fused multiply-add
# moved samples across a bin edge, so the same data gave different trim in two builds. Not for the
# rest of the core: test_vr_view compares vr_math with the game's own types.c, which is compiled fused.
if(NOT MSVC)
	set_source_files_properties(src/vr/vr_cockpit_core.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
target_link_libraries(wipeout_vr_core PUBLIC sfvr OpenXR::headers m)


# The game
target_sources(wipeout PRIVATE
	src/vr/vr_cockpit.c
	src/vr/vr_cockpit.h
	src/vr/vr.c
	src/vr/vr.h
	src/vr/vr_gl.c
	src/vr/vr_gl.h
	src/vr/vr_render.h
	src/vr/vr_render_gl.inc
	src/vr/vr_shaders_gl330.h
	src/vr/vr_xr_loader.c
)
target_compile_definitions(wipeout PRIVATE WIPEOUT_VR=1)
target_include_directories(wipeout PRIVATE src/vr)
target_link_libraries(wipeout PRIVATE wipeout_vr_core OpenXR::openxr_loader)
if(NOT MSVC)
	# ARM64 `char` is unsigned; keep x86's behaviour
	target_compile_options(wipeout PRIVATE -fsigned-char)
endif()
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
	# vr_gl.c includes the Xlib and GLX headers for the graphics binding. It
	# reaches libGL and libX11 through SDL, so there is nothing to link.
	find_package(X11 REQUIRED)
	target_include_directories(wipeout PRIVATE ${X11_INCLUDE_DIR})
	# The static OpenXR loader is C++: do not depend on the target's libstdc++.
	target_link_options(wipeout PRIVATE -static-libstdc++ -static-libgcc)
	# The package (tools/package_linux.sh) ships SDL2 and GLEW in lib/ beside the binary
	set_target_properties(wipeout PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/lib")
endif()


# Unit tests (fake runtime, no GL, no window, no game data)
foreach(name xr settings math clock paths stage compose refresh)
	add_executable(test_vr_${name} tests/vr/test_vr_${name}.c)
	set_property(TARGET test_vr_${name} PROPERTY C_STANDARD 11)
	target_include_directories(test_vr_${name} PRIVATE third_party/sfvr/tests)
	target_link_libraries(test_vr_${name} PRIVATE wipeout_vr_core)
	target_compile_options(test_vr_${name} PRIVATE
		$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra>
	)
	add_test(NAME vr_${name} COMMAND test_vr_${name})
endforeach()

# The eye and HUD maths against the game's own matrices: src/types.c is the oracle
add_executable(test_vr_view tests/vr/test_vr_view.c src/types.c)
set_property(TARGET test_vr_view PROPERTY C_STANDARD 11)
target_include_directories(test_vr_view PRIVATE src third_party/sfvr/tests)
target_link_libraries(test_vr_view PRIVATE wipeout_vr_core)
target_compile_options(test_vr_view PRIVATE
	$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra>
)
add_test(NAME vr_view COMMAND test_vr_view)

# Cockpit H: the table, the cut, the trim rule, the parts, the eye and the bank modes
add_executable(test_vr_cockpit tests/vr/test_vr_cockpit.c src/types.c)
set_property(TARGET test_vr_cockpit PROPERTY C_STANDARD 11)
target_include_directories(test_vr_cockpit PRIVATE src third_party/sfvr/tests)
target_link_libraries(test_vr_cockpit PRIVATE wipeout_vr_core)
target_compile_options(test_vr_cockpit PRIVATE
	$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra>
)
add_test(NAME vr_cockpit COMMAND test_vr_cockpit)

# The committed C table is what the committed JSON generates (no game data, no browser)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
	add_test(NAME vr_cockpit_geometry COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mockup/check_cockpit_geometry.py)
endif()


# Host acceptance harness (Linux; needs a GL context, so it runs under Xvfb with
# Mesa llvmpipe). It links the real renderer (src/render_gl.c, WIPEOUT_VR) and
# drives the record, replay, HUD alpha and dump paths with a synthetic scene and
# a fake view provider. Output goes to <build>/vr-harness/.
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
	# The cockpit tests (tests/vr/vr_cockpit_harness.inc) also link the game's own
	# image loader, object structs, UI, HUD and the cockpit module, with the
	# game's globals stubbed in the test.
	add_executable(vr_render_harness
		tests/vr/vr_render_harness.c
		tests/vr/vr_cockpit_harness.inc
		src/render_gl.c
		src/types.c
		src/mem.c
		src/utils.c
		src/wipeout/image.c
		src/wipeout/object.c
		src/wipeout/ui.c
		src/wipeout/hud.c
		src/vr/vr_cockpit.c
	)
	set_source_files_properties(src/wipeout/image.c src/wipeout/object.c src/wipeout/ui.c src/wipeout/hud.c
		PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-parameter")
	set_property(TARGET vr_render_harness PROPERTY C_STANDARD 11)
	target_compile_definitions(vr_render_harness PRIVATE WIPEOUT_VR=1 RENDERER_GL)
	target_include_directories(vr_render_harness PRIVATE src src/vr tests/vr third_party/sfvr/tests ${GLEW_INCLUDE_DIRS})
	target_include_directories(vr_render_harness SYSTEM PRIVATE src/libs)
	target_link_libraries(vr_render_harness PRIVATE wipeout_vr_core GLEW::GLEW SDL2::Main m)
	if(USE_GLVND AND TARGET OpenGL::OpenGL)
		target_link_libraries(vr_render_harness PRIVATE OpenGL::OpenGL)
	else()
		target_link_libraries(vr_render_harness PRIVATE OpenGL::GL)
	endif()
	target_compile_options(vr_render_harness PRIVATE -Wall -Wextra -fsigned-char)

	find_program(WIPEOUT_VR_XVFB_RUN xvfb-run)
	if(WIPEOUT_VR_XVFB_RUN)
		add_test(NAME vr_render_harness COMMAND
			${WIPEOUT_VR_XVFB_RUN} -a -s "-screen 0 1600x1200x24"
			$<TARGET_FILE:vr_render_harness> ${CMAKE_BINARY_DIR}/vr-harness)
		set_tests_properties(vr_render_harness PROPERTIES
			ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEODRIVER=x11"
			TIMEOUT 900)
	else()
		message(STATUS "vr_render_harness is built but not registered with CTest: xvfb-run was not found")
	endif()
endif()
# The input tests run the game's real input layer (src/input.c, which needs nothing else)
foreach(name input input_xr)
	add_executable(test_vr_${name} tests/vr/test_vr_${name}.c src/input.c)
	set_property(TARGET test_vr_${name} PROPERTY C_STANDARD 11)
	target_include_directories(test_vr_${name} PRIVATE third_party/sfvr/tests src)
	target_link_libraries(test_vr_${name} PRIVATE wipeout_vr_core)
	target_compile_options(test_vr_${name} PRIVATE
		$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra>
	)
	add_test(NAME vr_${name} COMMAND test_vr_${name})
endforeach()
