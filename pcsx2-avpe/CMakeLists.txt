set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

add_executable(avpe
	EmulationThread.cpp
	EmulationThread.h
	HostBackend.h
	HostServices.cpp
	HostInputRouter.cpp
	HostInputRouter.h
	HostWindow.cpp
	HostWindow.h
	Main.cpp
	NativeWindow.cpp
	NativeWindow.h
	RenderSurface.cpp
	RenderSurface.h
	Runtime.cpp
	Runtime.h
	Settings.cpp
	Settings.h
)

target_include_directories(avpe PRIVATE
	${Qt6Gui_PRIVATE_INCLUDE_DIRS}
	"${CMAKE_BINARY_DIR}/common/include"
	"${CMAKE_SOURCE_DIR}"
)
target_link_libraries(avpe PRIVATE
	PCSX2_FLAGS
	PCSX2
	lucent::lucent
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
)
if(NOT WIN32 AND NOT APPLE)
	target_link_libraries(avpe PRIVATE Qt6::GuiPrivate)
endif()
target_compile_definitions(avpe PRIVATE QT_NO_EXCEPTIONS)

fixup_file_properties(avpe)
setup_main_executable(avpe STANDALONE NO_DEPLOY_CODESIGN
	INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

# The AVPE host owns a relocatable install boundary for hosted/package checks.
# PCSX2's legacy developer install rules target its source-tree bin directory;
# this product rule keeps the standalone host packageable on every desktop OS.
if(UNIX AND NOT APPLE)
	list(GET CMAKE_PREFIX_PATH 0 avpe_dependency_prefix)
	set_target_properties(avpe PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
	install(TARGETS avpe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT avpe)
	foreach(avpe_libdir lib lib64)
		install(DIRECTORY "${avpe_dependency_prefix}/${avpe_libdir}/"
			DESTINATION ${CMAKE_INSTALL_LIBDIR}
			COMPONENT avpe
			OPTIONAL FILES_MATCHING PATTERN "*.so*")
	endforeach()
	install(DIRECTORY "${avpe_dependency_prefix}/plugins/"
		DESTINATION plugins
		COMPONENT avpe
		OPTIONAL)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/qt.conf"
		DESTINATION ${CMAKE_INSTALL_BINDIR}
		COMPONENT avpe)
else()
	install(TARGETS avpe
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT avpe
		BUNDLE DESTINATION . COMPONENT avpe)
endif()
if(NOT APPLE)
	install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/resources/"
		DESTINATION "${CMAKE_INSTALL_BINDIR}/resources"
		COMPONENT avpe
		OPTIONAL
	)
endif()
