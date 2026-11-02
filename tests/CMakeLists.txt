find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

function(tweakpng_add_test name)
	add_executable(${name} ${name}.cpp pngbuilder.h ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
	target_link_libraries(${name} PRIVATE tweakpng_ui Qt${QT_VERSION_MAJOR}::Test ZLIB::ZLIB)
	target_compile_definitions(${name} PRIVATE TWEAKPNG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
	add_test(NAME ${name} COMMAND ${name})
	# Tests run without a display on every platform.
	set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

tweakpng_add_test(test_core)
tweakpng_add_test(test_ui)
