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

	using AVPE::NativeInput::SelectionMode;
	constexpr SelectionMode Replace = SelectionMode::Replace;

	struct Edge
	{
		MouseButton button;
		ButtonEdge edge;
		SelectionMode selection = SelectionMode::Replace;
		bool operator==(const Edge&) const = default;
	};

	class HostPointerInputTest : public testing::Test
	{
	protected:
		bool menu_open = false;
		bool on_minimap = false;
		int jumps = 0;
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
				.button_edge = [this](const MouseButton button, const ButtonEdge edge, const SelectionMode selection) {
					edges.push_back({button, edge, selection});
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
					if (action == NativeCameraInput::Action::Jump)
					{
						jumps += on_minimap ? 1 : 0;
						result.status = on_minimap ? NativeCameraInput::Status::Success : NativeCameraInput::Status::OffMinimap;
					}
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
		EXPECT_TRUE(input.Release(HostPointerInput::Button::Primary, Replace));
		EXPECT_TRUE(input.Press(HostPointerInput::Button::Secondary));
		EXPECT_TRUE(input.Release(HostPointerInput::Button::Secondary, Replace));
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
		input.Release(HostPointerInput::Button::Primary, Replace);
		input.DoubleClick(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary, Replace);
		const std::vector<Edge> expected{
			{MouseButton::Primary, ButtonEdge::Press},
			{MouseButton::Primary, ButtonEdge::Release},
			{MouseButton::Primary, ButtonEdge::Press},
			{MouseButton::Primary, ButtonEdge::Release},
		};
		EXPECT_EQ(edges, expected);
	}

	TEST_F(HostPointerInputTest, CursorAtTheEdgeScrollsTheCamera)
	{
		input.Move(0.999f, 0.5f);
		input.Tick();
		input.Move(0.0f, 0.0f);
		input.Tick();
		input.Move(0.5f, 0.5f);
		input.Tick();
		const std::vector<std::pair<float, float>> expected{{1.0f, 0.0f}, {-1.0f, -1.0f}};
		EXPECT_EQ(camera_moves, expected);
	}

	TEST_F(HostPointerInputTest, EdgeScrollStopsOutsideTheWindowAndInMenus)
	{
		input.Move(0.0f, 0.5f);
		input.Leave();
		input.Tick();
		input.Move(0.0f, 0.5f);
		menu_open = true;
		input.Tick();
		EXPECT_TRUE(camera_moves.empty());
	}

	TEST_F(HostPointerInputTest, MinimapClickAndDragMoveTheCameraNotTheSelection)
	{
		on_minimap = true;
		input.Move(0.8f, 0.8f);
		input.Press(HostPointerInput::Button::Primary);
		input.Move(0.82f, 0.8f);
		input.Release(HostPointerInput::Button::Primary, Replace);
		EXPECT_EQ(jumps, 2);
		EXPECT_TRUE(edges.empty());
		on_minimap = false;
		input.Move(0.5f, 0.5f);
		input.Press(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary, Replace);
		EXPECT_EQ(edges.size(), 2u);
	}

	TEST_F(HostPointerInputTest, ShiftAndCtrlReachOnlyTheMissionPrimaryRelease)
	{
		input.Press(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary, SelectionMode::Toggle);
		input.Press(HostPointerInput::Button::Secondary);
		input.Release(HostPointerInput::Button::Secondary, SelectionMode::SameType);
		const std::vector<Edge> expected{
			{MouseButton::Primary, ButtonEdge::Press},
			{MouseButton::Primary, ButtonEdge::Release, SelectionMode::Toggle},
			{MouseButton::Secondary, ButtonEdge::Press},
			{MouseButton::Secondary, ButtonEdge::Release},
		};
		EXPECT_EQ(edges, expected);
	}

	TEST_F(HostPointerInputTest, MenuDoubleClickActivatesOnce)
	{
		menu_open = true;
		input.Press(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary, Replace);
		input.DoubleClick(HostPointerInput::Button::Primary);
		input.Release(HostPointerInput::Button::Primary, Replace);
		EXPECT_EQ(menu_activations, 1);
		EXPECT_TRUE(edges.empty());
	}
} // namespace
