#include "pcsx2-avpe/HostPointerInput.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
	using AVPE::HostPointerInput;
	using AVPE::NativeInput::ButtonEdge;
	using AVPE::NativeInput::MouseButton;
	namespace NativeMenuInput = AVPE::NativeMenuInput;
	namespace NativeInput = AVPE::NativeInput;
	namespace NativeCameraInput = AVPE::NativeCameraInput;

	struct Edge
	{
		MouseButton button;
		ButtonEdge edge;
		bool operator==(const Edge&) const = default;
	};

	class HostPointerInputTest : public testing::Test
	{
	protected:
		bool menu_open = false;
		int menu_activations = 0;
		std::vector<Edge> edges;
		std::vector<std::pair<float, float>> pointer_moves;
		std::vector<std::pair<float, float>> camera_moves;
		HostPointerInput input{Guest()};

		HostPointerInput::Guest Guest()
		{
			return {
				.inspect_menu = [this] {
					NativeMenuInput::Result result;
					result.status = menu_open ? NativeMenuInput::Status::Success : NativeMenuInput::Status::MenuUnavailable;
					return result; },
				.move_menu_pointer = [](float, float) { return Succeeded<NativeMenuInput::PointerResult>(); },
				.activate_menu_pointer = [this] {
					++menu_activations;
					return Succeeded<NativeMenuInput::PointerResult>(); },
				.move_pointer = [this](const float x, const float y) {
					pointer_moves.emplace_back(x, y);
					NativeInput::Result result;
					result.status = NativeInput::Status::Success;
					return result; },
				.button_edge = [this](const MouseButton button, const ButtonEdge edge) {
					edges.push_back({button, edge});
					NativeInput::ButtonResult result;
					result.status = NativeInput::Status::Success;
					return result; },
				.camera = [this](const NativeCameraInput::Action action, const float x, const float y) {
					if (action == NativeCameraInput::Action::Move)
					{
						camera_moves.emplace_back(x, y);
					}
					NativeCameraInput::Result result;
					result.status = NativeCameraInput::Status::Success;
					return result; },
			};
		}

		template <typename Result>
		static Result Succeeded()
		{
			Result result;
			result.status = NativeMenuInput::Status::Success;
			return result;
		}
	};

	TEST_F(HostPointerInputTest, MissionClicksReachTheGuestPointer)
	{
		EXPECT_TRUE(input.Press(HostPointerInput::Button::Primary));
		EXPECT_TRUE(input.Release(HostPointerInput::Button::Primary));
		EXPECT_TRUE(input.Press(HostPointerInput::Button::Secondary));
		EXPECT_TRUE(input.Release(HostPointerInput::Button::Secondary));
		const std::vector<Edge> expected{
			{MouseButton::Primary, ButtonEdge::Press},
			{MouseButton::Primary, ButtonEdge::Release},
			{MouseButton::Secondary, ButtonEdge::Press},
			{MouseButton::Secondary, ButtonEdge::Release},
		};
		EXPECT_EQ(edges, expected);
	}

	TEST_F(HostPointerInputTest, MissionDoubleClickIsASecondClick)
	{
		// The guest times the two releases itself (GAvPPointer::SelectChanging, 0.5 s).
		input.Press(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary);
		input.DoubleClick(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary);
		const std::vector<Edge> expected{
			{MouseButton::Primary, ButtonEdge::Press},
			{MouseButton::Primary, ButtonEdge::Release},
			{MouseButton::Primary, ButtonEdge::Press},
			{MouseButton::Primary, ButtonEdge::Release},
		};
		EXPECT_EQ(edges, expected);
	}

	TEST_F(HostPointerInputTest, MenuDoubleClickActivatesOnce)
	{
		menu_open = true;
		input.Press(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary);
		input.DoubleClick(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary);
		EXPECT_EQ(menu_activations, 1);
		EXPECT_TRUE(edges.empty());
	}
} // namespace
