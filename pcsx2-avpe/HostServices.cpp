// PCSX2 core callbacks implemented by the standalone AVPE frontend.
#include "pcsx2-avpe/EmulationThread.h"
#include "pcsx2-avpe/Runtime.h"
#include "pcsx2-avpe/Settings.h"

#include "pcsx2/Achievements.h"
#include "pcsx2/GS.h"
#include "pcsx2/GameList.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/VMManager.h"

#include "AVPE/NativeBiosTrace.h"

#include "common/ProgressCallback.h"

#include <lucent/log.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>

#include <algorithm>
#include <cstring>

void Host::CommitBaseSettingChanges()
{
	AVPE::Settings::Save();
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
	(void)si;
	(void)lock;
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
	(void)old_config;
}

bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
	(void)folders;
	(void)core;
	(void)controllers;
	(void)hotkeys;
	(void)ui;
	return false;
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
	si.SetBoolValue("UI", "InhibitScreensaver", true);
	si.SetBoolValue("UI", "StartFullscreen", false);
	si.SetBoolValue("UI", "PauseOnFocusLoss", false);
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
	return ProgressCallback::CreateNullProgressCallback();
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
	lucent::info("avpe-core", "{}: {}", title, message);
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
	lucent::error("avpe-core", "{}: {}", title, message);
}

void Host::OpenURL(const std::string_view url)
{
	QMetaObject::invokeMethod(QCoreApplication::instance(), [url = std::string(url)]() { QDesktopServices::openUrl(QUrl(QString::fromStdString(url))); }, Qt::QueuedConnection);
}

bool Host::InBatchMode()
{
	return true;
}

bool Host::InNoGUIMode()
{
	return false;
}

bool Host::CopyTextToClipboard(const std::string_view text)
{
	QMetaObject::invokeMethod(QCoreApplication::instance(), [text = std::string(text)]() {
		if (QClipboard* const clipboard = QGuiApplication::clipboard())
			clipboard->setText(QString::fromStdString(text)); }, Qt::QueuedConnection);
	return true;
}

std::string Host::GetTextFromClipboard()
{
	std::string text;
	QMetaObject::invokeMethod(QCoreApplication::instance(), [&text]() {
		if (const QClipboard* const clipboard = QGuiApplication::clipboard())
			text = clipboard->text().toStdString(); }, Qt::BlockingQueuedConnection);
	return text;
}

void Host::BeginTextInput()
{
	QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
		if (QInputMethod* const input = QGuiApplication::inputMethod())
			input->show(); }, Qt::QueuedConnection);
}

void Host::EndTextInput()
{
	QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
		if (QInputMethod* const input = QGuiApplication::inputMethod())
			input->hide(); }, Qt::QueuedConnection);
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
	return AVPE::Runtime::Get()->GetTopLevelWindowInfo();
}

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
	lucent::info("avpe-input", "device connected: {} ({})", identifier, device_name);
}

void Host::OnInputDeviceDisconnected(const InputBindingKey key, const std::string_view identifier)
{
	(void)key;
	lucent::info("avpe-input", "device disconnected: {}", identifier);
}

void Host::SetMouseMode(const bool relative_mode, const bool hide_cursor)
{
	AVPE::Runtime::Get()->RequestMouseMode(relative_mode, hide_cursor);
}

void Host::SetMouseLock(const bool state)
{
	AVPE::Runtime::Get()->RequestMouseLock(state);
}

std::optional<WindowInfo> Host::AcquireRenderWindow(const bool recreate_window)
{
	return AVPE::Runtime::Get()->AcquireRenderWindow(recreate_window);
}

void Host::ReleaseRenderWindow()
{
	AVPE::Runtime::Get()->ReleaseRenderWindow();
}

void Host::BeginPresentFrame()
{
}

void Host::RequestResizeHostDisplay(const s32 width, const s32 height)
{
	AVPE::Runtime::Get()->RequestResize(width, height);
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
}

void Host::OnVMDestroyed()
{
	AVPE::Runtime::RequestApplicationExit(EXIT_SUCCESS);
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
	AVPE::Runtime::Get()->GetEmulationThread().Wake();
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override,
	const std::string& disc_path, const std::string& disc_serial, u32 disc_crc, u32 current_crc)
{
	(void)elf_override;
	(void)disc_path;
	(void)disc_serial;
	(void)disc_crc;
	(void)current_crc;
	if (!title.empty())
		lucent::info("avpe-core", "running {}", title);
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
	(void)filename;
}

void Host::OnSaveStateLoaded(const std::string_view filename, const bool was_successful)
{
	(void)filename;
	if (was_successful)
		AVPE::NativeBiosTrace::Reset();
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
	(void)filename;
}

void Host::RunOnCPUThread(std::function<void()> function, const bool block)
{
	AVPE::Runtime::Get()->GetEmulationThread().Run(std::move(function), block);
}

void Host::RunOnGSThread(std::function<void()> function)
{
	MTGS::RunOnGSThread(std::move(function));
}

void Host::RefreshGameListAsync(const bool invalidate_cache)
{
	(void)invalidate_cache;
}

void Host::CancelGameListRefresh()
{
}

bool Host::IsFullscreen()
{
	return AVPE::Runtime::Get()->IsFullscreen();
}

void Host::SetFullscreen(const bool enabled)
{
	AVPE::Runtime::Get()->SetFullscreen(enabled);
}

void Host::OnCaptureStarted(const std::string& filename)
{
	(void)filename;
}

void Host::OnCaptureStopped()
{
}

void Host::RequestExitApplication(const bool allow_confirm)
{
	(void)allow_confirm;
	AVPE::Runtime::Get()->RequestExit();
}

void Host::RequestExitBigPicture()
{
	AVPE::Runtime::Get()->RequestExit();
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
	(void)allow_confirm;
	(void)allow_save_state;
	(void)default_save_state;
	AVPE::Runtime::Get()->RequestExit();
}

void Host::PumpMessagesOnCPUThread()
{
	AVPE::Runtime::Get()->GetEmulationThread().PumpEvents();
}

s32 Host::Internal::GetTranslatedStringImpl(
	const std::string_view context, const std::string_view msg, char* tbuf, const size_t tbuf_space)
{
	(void)context;
	if (msg.size() > tbuf_space)
		return -1;
	if (msg.empty())
		return 0;
	std::memcpy(tbuf, msg.data(), msg.size());
	return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(
	const char* context, const char* msg, const char* disambiguation, const int count)
{
	(void)context;
	(void)disambiguation;
	TinyString count_string = TinyString::from_format("{}", count);
	std::string result(msg);
	for (std::string::size_type position = result.find("%n"); position != std::string::npos;
		position = result.find("%n"))
	{
		result.replace(position, 2, count_string.view());
	}
	return result;
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
	(void)reason;
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
	(void)username;
	(void)points;
	(void)sc_points;
	(void)unread_messages;
}

void Host::OnAchievementsRefreshed()
{
}

void Host::OnAchievementsHardcoreModeChanged(const bool enabled)
{
	(void)enabled;
}

bool Host::LocaleCircleConfirm()
{
	return false;
}

bool Host::ShouldPreferHostFileSelector()
{
	return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory,
	FileSelectorCallback callback, FileSelectorFilters filters, std::string_view initial_directory)
{
	(void)title;
	(void)select_directory;
	(void)initial_directory;
	QMetaObject::invokeMethod(QCoreApplication::instance(), [callback = std::move(callback), filters = std::move(filters)]() mutable {
			(void)filters;
			callback(std::string()); }, Qt::QueuedConnection);
}

int Host::LocaleSensitiveCompare(const std::string_view lhs, const std::string_view rhs)
{
	const size_t shared = std::min(lhs.size(), rhs.size());
	const int comparison = std::strncmp(lhs.data(), rhs.data(), shared);
	if (comparison != 0)
		return comparison;
	return lhs.size() > rhs.size() ? 1 : lhs.size() < rhs.size() ? -1 :
	                                                               0;
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view str)
{
	(void)str;
	return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(const u32 code)
{
	(void)code;
	return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(const u32 code)
{
	(void)code;
	return nullptr;
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()
