#include "pcsx2/AVPE/NativePromptPlacement.h"

#include <gtest/gtest.h>

#include <cstring>
#include <utility>
#include <map>

namespace
{
	using AVPE::NativePromptGlyphs;
	using AVPE::NativePromptPlacement;
	using AVPE::PromptFrame;
	using AVPE::PromptKey;

	std::map<u32, u8> g_memory;

	bool ReadFake(const u32 address, void* const destination, const u32 size)
	{
		auto* const bytes = static_cast<u8*>(destination);
		for (u32 index = 0; index < size; index++)
		{
			const auto found = g_memory.find(address + index);
			if (found == g_memory.end())
			{
				return false;
			}
			bytes[index] = found->second;
		}
		return true;
	}

	template <typename T>
	void Write(const u32 address, const T value)
	{
		u8 bytes[sizeof(T)];
		std::memcpy(bytes, &value, sizeof(T));
		for (u32 index = 0; index < sizeof(T); index++)
		{
			g_memory[address + index] = bytes[index];
		}
	}

	constexpr u32 SymbolTable = 0x00367358;
	constexpr u32 Buckets = 0x01000000;
	constexpr u32 Entries = 0x01001000;
	constexpr u32 BackMesh = 0x0135EA3C;
	constexpr u32 SelectMesh = 0x0135EB7C;
	constexpr u32 Workspace = 0x01200000;
	constexpr u32 Stack = 0x01FF0000;
	constexpr u32 PauseWindow = 7;
	constexpr u32 BackItem = 0x012E9540;
	constexpr u32 SelectItem = 0x012E8A60;
	constexpr u32 PatrolItem = 0x01300000;
	constexpr u32 GatherItem = 0x01301000;
	constexpr u32 Labels = 0x01360000;

	// A GMenuItem with its embedded CRender node, hotkey and label.
	void AddItem(const u32 item, const u32 hotkey, const char* const label)
	{
		const u32 text = Labels + (item & 0xFFFFF);
		Write<u32>(item + NativePromptPlacement::ItemHotKeyOffset, hotkey);
		Write<u32>(item + NativePromptPlacement::ItemTextOffset, text);
		for (u32 index = 0; label[index] != 0; index++)
		{
			Write<char>(text + index, label[index]);
		}
		Write<char>(text + static_cast<u32>(std::strlen(label)), 0);
	}

	// Lays out MASTER.TBD's Top/BottomButton symbols and the live pause-menu frame state.
	void BuildPauseMenu()
	{
		g_memory.clear();
		Write<u32>(NativePromptGlyphs::SymbolTablePointer, SymbolTable);
		Write<u32>(SymbolTable, 0xFF);
		Write<u32>(SymbolTable + 4, Buckets);
		for (u32 bucket = 0; bucket < 0x100; bucket++)
		{
			Write<u32>(Buckets + bucket * NativePromptGlyphs::BucketSize, Entries + bucket * 0x40);
			Write<s32>(Buckets + bucket * NativePromptGlyphs::BucketSize + 4, -1);
		}
		const auto add_symbol = [](const u32 key, const u32 value) {
			const u32 bucket = Buckets + (key & 0xFF) * NativePromptGlyphs::BucketSize;
			const u32 entries = Entries + (key & 0xFF) * 0x40;
			// An unrelated symbol first, so the scan has to walk the bucket.
			Write<u32>(entries, 0x00100000);
			Write<u32>(entries + 4, key ^ 0x100);
			Write<u32>(entries + NativePromptGlyphs::EntrySize, value);
			Write<u32>(entries + NativePromptGlyphs::EntrySize + 4, key);
			Write<s32>(bucket + 4, 1);
		};
		add_symbol(0x5F8391BF, BackMesh);
		add_symbol(0xA67152A0, SelectMesh);
		Write<u32>(BackMesh, NativePromptGlyphs::CRendPS2MeshVtable);
		Write<u32>(SelectMesh, NativePromptGlyphs::CRendPS2MeshVtable);

		Write<s32>(NativePromptPlacement::ResolutionWidthAddress, 640);
		Write<s32>(NativePromptPlacement::ResolutionHeightAddress, 448);
		Write<u32>(NativePromptPlacement::CurrentWindowAddress, PauseWindow);
		const u32 window = NativePromptPlacement::WindowDataBase + PauseWindow * NativePromptPlacement::WindowDataStride;
		Write<float>(window + NativePromptPlacement::WindowCentreXOffset, 320.0f);
		Write<float>(window + NativePromptPlacement::WindowCentreYOffset, 240.0f);
		// EndFrame writes the slot origin as (cx + 1728, cy + 1824 - 8).
		const u32 slot = NativePromptPlacement::ViewportDataBase + PauseWindow * NativePromptPlacement::ViewportDataStride +
		                 NativePromptPlacement::SlotDrawOriginOffset;
		Write<float>(slot, 320.0f + 1728.0f);
		Write<float>(slot + 4, 240.0f + 1824.0f - 8.0f);

		AddItem(BackItem, AVPE::NativePromptKeys::FrontEndBack, "Back");
		AddItem(SelectItem, AVPE::NativePromptKeys::FrontEndSelect, "Select");
		AddItem(PatrolItem, 0xC134080A, "Patrol");
		AddItem(GatherItem, 0xB8697E8E, "Gather");
	}

	void DrawMesh(NativePromptPlacement& placement, const u32 mesh, const u32 item, const s32 xmin, const s32 xmax)
	{
		Write<u32>(Workspace + NativePromptPlacement::WorkspaceRenderOffset, item + NativePromptPlacement::ItemRenderOffset);
		Write<u32>(Workspace + NativePromptPlacement::WorkspaceResourceOffset, mesh);
		const u32 rect = Stack + NativePromptPlacement::RectStackOffset;
		Write<s32>(rect + 0x0, xmin);
		Write<s32>(rect + 0x4, 391);
		Write<s32>(rect + 0x8, xmax);
		Write<s32>(rect + 0xC, 415);
		placement.ObserveGetMatrixRect(Workspace, Stack, ReadFake);
	}

	TEST(NativePromptPlacementTest, DrawOffsetIsTheEndFrameOriginLessTheCentres)
	{
		const NativePromptPlacement::DrawOffset offset =
			NativePromptPlacement::ComputeDrawOffset(2048.0f, 2056.0f, 320.0f, 240.0f, 640.0f, 448.0f);

		EXPECT_FLOAT_EQ(offset.x, 0.0f);
		EXPECT_FLOAT_EQ(offset.y, -8.0f);
	}

	TEST(NativePromptPlacementTest, FindsSymbolsThroughTheGuestHashTable)
	{
		BuildPauseMenu();

		EXPECT_EQ(NativePromptGlyphs::FindSymbol(0xA67152A0, ReadFake), SelectMesh);
		EXPECT_EQ(NativePromptGlyphs::FindSymbol(0x5F8391BF, ReadFake), BackMesh);
		EXPECT_FALSE(NativePromptGlyphs::FindSymbol(0x361BAEBF, ReadFake).has_value());
	}

	TEST(NativePromptPlacementTest, PublishesDrawnGlyphsWhereVu1PlacesThem)
	{
		BuildPauseMenu();
		std::vector<PromptFrame> published;
		NativePromptPlacement placement([&published](PromptFrame frame) { published.push_back(std::move(frame)); });

		// The first kick resolves the glyph meshes; the second frame's draws are matched.
		placement.ObserveFrameKick(ReadFake);
		DrawMesh(placement, SelectMesh, SelectItem, 98, 122);
		DrawMesh(placement, BackMesh, BackItem, 158, 182);
		DrawMesh(placement, 0x01400000, SelectItem, 0, 10);
		placement.ObserveFrameKick(ReadFake);

		ASSERT_EQ(published.size(), 2u);
		EXPECT_TRUE(published[0].prompts.empty());
		const PromptFrame& frame = published[1];
		EXPECT_FLOAT_EQ(frame.framebuffer_height, 448.0f);
		ASSERT_EQ(frame.prompts.size(), 2u);
		EXPECT_EQ(frame.prompts[0].key.kind, PromptKey::Kind::Confirm);
		EXPECT_EQ(frame.prompts[0].item, SelectItem);
		EXPECT_EQ(frame.prompts[1].key.kind, PromptKey::Kind::Back);
		EXPECT_EQ(frame.prompts[1].item, BackItem);
		EXPECT_FLOAT_EQ(frame.prompts[1].left, 158.0f);
		EXPECT_FLOAT_EQ(frame.prompts[1].top, 383.0f);
		EXPECT_FLOAT_EQ(frame.prompts[1].bottom, 407.0f);
	}

	TEST(NativePromptPlacementTest, KeysEachPromptByItsItemNotItsGlyph)
	{
		BuildPauseMenu();
		std::vector<PromptFrame> published;
		NativePromptPlacement placement([&published](PromptFrame frame) { published.push_back(std::move(frame)); });

		// A command panel draws the Select and Back glyphs for Patrol and Gather.
		placement.ObserveFrameKick(ReadFake);
		DrawMesh(placement, SelectMesh, PatrolItem, 98, 122);
		DrawMesh(placement, BackMesh, GatherItem, 158, 182);
		placement.ObserveFrameKick(ReadFake);

		const PromptFrame& frame = published[1];
		ASSERT_EQ(frame.prompts.size(), 2u);
		EXPECT_EQ(frame.prompts[0].key.kind, PromptKey::Kind::Command);
		EXPECT_EQ(frame.prompts[0].key.letter, 'P');
		EXPECT_EQ(frame.prompts[1].key.letter, 'G');
		EXPECT_EQ(frame.ItemForLetter('G'), GatherItem);
		EXPECT_FALSE(frame.ItemForLetter('S').has_value());
	}

	TEST(NativePromptPlacementTest, AFrameWithoutGlyphsClearsThePrompts)
	{
		BuildPauseMenu();
		std::vector<PromptFrame> published;
		NativePromptPlacement placement([&published](PromptFrame frame) { published.push_back(std::move(frame)); });

		placement.ObserveFrameKick(ReadFake);
		DrawMesh(placement, SelectMesh, SelectItem, 98, 122);
		placement.ObserveFrameKick(ReadFake);
		placement.ObserveFrameKick(ReadFake);

		ASSERT_EQ(published.size(), 3u);
		EXPECT_EQ(published[1].prompts.size(), 1u);
		EXPECT_TRUE(published[2].prompts.empty());
	}

	TEST(NativePromptPlacementTest, IgnoresAGlyphSymbolThatIsNotAMesh)
	{
		BuildPauseMenu();
		Write<u32>(SelectMesh, 0x00340000);
		std::vector<PromptFrame> published;
		NativePromptPlacement placement([&published](PromptFrame frame) { published.push_back(std::move(frame)); });

		placement.ObserveFrameKick(ReadFake);
		DrawMesh(placement, SelectMesh, SelectItem, 98, 122);
		placement.ObserveFrameKick(ReadFake);

		EXPECT_TRUE(published[1].prompts.empty());
	}
} // namespace
