#include "pcsx2-avpe/NativeWindow.h"

#include <gtest/gtest.h>

namespace
{
	TEST(NativeWindowHandlesTest, SurfacelessNeedsNoNativeHandles)
	{
		WindowInfo info;
		info.type = WindowInfo::Type::Surfaceless;

		EXPECT_TRUE(AVPE::NativeWindow::HasRequiredNativeHandles(info));
	}

	TEST(NativeWindowHandlesTest, X11AndWaylandRequireDisplayAndWindow)
	{
		int display = 0;
		int window = 0;
		for (const WindowInfo::Type type : {WindowInfo::Type::X11, WindowInfo::Type::Wayland})
		{
			WindowInfo info;
			info.type = type;
			info.display_connection = &display;
			info.window_handle = &window;
			EXPECT_TRUE(AVPE::NativeWindow::HasRequiredNativeHandles(info));

			info.display_connection = nullptr;
			EXPECT_FALSE(AVPE::NativeWindow::HasRequiredNativeHandles(info));
			info.display_connection = &display;
			info.window_handle = nullptr;
			EXPECT_FALSE(AVPE::NativeWindow::HasRequiredNativeHandles(info));
		}
	}

	TEST(NativeWindowHandlesTest, Win32AndMacOSRequireWindow)
	{
		int window = 0;
		for (const WindowInfo::Type type : {WindowInfo::Type::Win32, WindowInfo::Type::MacOS})
		{
			WindowInfo info;
			info.type = type;
			info.window_handle = &window;
			EXPECT_TRUE(AVPE::NativeWindow::HasRequiredNativeHandles(info));

			info.window_handle = nullptr;
			EXPECT_FALSE(AVPE::NativeWindow::HasRequiredNativeHandles(info));
		}
	}
} // namespace
