set(CMAKE_AUTOMOC ON)

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
setup_main_executable(avpe)
