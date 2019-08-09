#include <Windows.h>
#include <shellapi.h>

#include <cstdio>

#include "ui.h"
#include "resource.h"
#include "mapping.h"
#include "settings.h"
#include "rfu.h"

#define RFU_TRAYICON			WM_APP + 1
#define RFU_TRAYMENU_APC		WM_APP + 2
#define RFU_TRAYMENU_CONSOLE	WM_APP + 3
#define RFU_TRAYMENU_EXIT		WM_APP + 4
#define RFU_TRAYMENU_VSYNC		WM_APP + 5
#define RFU_TRAYMENU_LOADSET	WM_APP + 6
#define RFU_TRAYMENU_GITHUB		WM_APP + 7
#define RFU_TRAYMENU_STUDIO		WM_APP + 8
#define RFU_TRAYMENU_CFU		WM_APP + 9

#define RFU_FCS_FIRST			(WM_APP + 10)
#define RFU_FCS_NONE			RFU_FCS_FIRST + 0
#define RFU_FCS_30				RFU_FCS_FIRST + 1
#define RFU_FCS_60				RFU_FCS_FIRST + 2
#define RFU_FCS_75				RFU_FCS_FIRST + 3
#define RFU_FCS_120				RFU_FCS_FIRST + 4
#define RFU_FCS_144				RFU_FCS_FIRST + 5
#define RFU_FCS_240				RFU_FCS_FIRST + 6
#define RFU_FCS_LAST			(RFU_FCS_240)

HWND UI::Window = NULL;
int UI::AttachedProcessesCount = 0;
bool UI::IsConsoleOnly = false;

HANDLE WatchThread;
NOTIFYICONDATA NotifyIconData;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case RFU_TRAYICON:
	{
		if (lParam == WM_RBUTTONDOWN || lParam == WM_LBUTTONDOWN)
		{
			POINT position;
			GetCursorPos(&position);

			HMENU popup = CreatePopupMenu();

			char buffer[64];
			sprintf_s(buffer, "Attached Processes: %d", UI::AttachedProcessesCount);

			AppendMenu(popup, MF_STRING | MF_GRAYED, RFU_TRAYMENU_APC, buffer);
			AppendMenu(popup, MF_SEPARATOR, 0, NULL);

#ifndef RFU_NO_DLL
			AppendMenu(popup, MF_STRING | (Settings::VSyncEnabled ? MF_CHECKED : 0), RFU_TRAYMENU_VSYNC, "Enable VSync");
#else
			AppendMenu(popup, MF_STRING | (Settings::UnlockStudio ? MF_CHECKED : 0), RFU_TRAYMENU_STUDIO, "Unlock Studio");
#endif
			AppendMenu(popup, MF_STRING | (Settings::CheckForUpdates ? MF_CHECKED : 0), RFU_TRAYMENU_CFU, "Check for Updates");

			HMENU submenu = CreatePopupMenu();
			AppendMenu(submenu, MF_STRING, RFU_FCS_NONE, "None");
			AppendMenu(submenu, MF_STRING, RFU_FCS_30, "30");
			AppendMenu(submenu, MF_STRING, RFU_FCS_60, "60");
			AppendMenu(submenu, MF_STRING, RFU_FCS_75, "75");
			AppendMenu(submenu, MF_STRING, RFU_FCS_120, "120");
			AppendMenu(submenu, MF_STRING, RFU_FCS_144, "144");
			AppendMenu(submenu, MF_STRING, RFU_FCS_240, "240");
			CheckMenuRadioItem(submenu, RFU_FCS_FIRST, RFU_FCS_LAST, RFU_FCS_FIRST + Settings::FPSCapSelection, MF_BYCOMMAND);

			AppendMenu(popup, MF_POPUP, (UINT_PTR)submenu, "FPS Cap");
			AppendMenu(popup, MF_SEPARATOR, 0, NULL);
			AppendMenu(popup, MF_STRING, RFU_TRAYMENU_LOADSET, "Load Settings");
			AppendMenu(popup, MF_STRING, RFU_TRAYMENU_CONSOLE, "Toggle Console");
			AppendMenu(popup, MF_STRING, RFU_TRAYMENU_GITHUB, "Visit GitHub");
			AppendMenu(popup, MF_STRING, RFU_TRAYMENU_EXIT, "Exit");

			SetForegroundWindow(hwnd); // to allow "clicking away"
			BOOL result = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_TOPALIGN | TPM_LEFTALIGN, position.x, position.y, 0, hwnd, NULL);

			if (result != 0)
			{
				switch (result)
				{
				case RFU_TRAYMENU_EXIT:
#ifdef RFU_NO_DLL
					SetFPSCapExternal(60);
#endif
					Shell_NotifyIcon(NIM_DELETE, &NotifyIconData);
					TerminateThread(WatchThread, 0);
					FreeConsole();
					PostQuitMessage(0);
					break;

				case RFU_TRAYMENU_CONSOLE:
					UI::ToggleConsole();
					break;

				case RFU_TRAYMENU_GITHUB:
					ShellExecuteA(NULL, "open", "https://github.com/" RFU_GITHUB_REPO, NULL, NULL, SW_SHOWNORMAL);
					break;

				case RFU_TRAYMENU_LOADSET:
					Settings::Load();
					Settings::Update();
					break;

#ifndef RFU_NO_DLL
				case RFU_TRAYMENU_VSYNC:
					Settings::VSyncEnabled = !Settings::VSyncEnabled;
					CheckMenuItem(popup, RFU_TRAYMENU_VSYNC, Settings::VSyncEnabled ? MF_CHECKED : MF_UNCHECKED);
					break;
#else
				case RFU_TRAYMENU_STUDIO:
					Settings::UnlockStudio = !Settings::UnlockStudio;
					CheckMenuItem(popup, RFU_TRAYMENU_STUDIO, Settings::UnlockStudio ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif
				case RFU_TRAYMENU_CFU:
					Settings::CheckForUpdates = !Settings::CheckForUpdates;
					CheckMenuItem(popup, RFU_TRAYMENU_CFU, Settings::CheckForUpdates ? MF_CHECKED : MF_UNCHECKED);
					break;

				default:
					if (result >= RFU_FCS_FIRST
						&& result <= RFU_FCS_LAST)
					{
						static double fcs_map[] = { 0.0, 30.0, 60.0, 75.0, 120.0, 144.0, 240.0 };
						Settings::FPSCapSelection = result - RFU_FCS_FIRST;
						Settings::FPSCap = fcs_map[Settings::FPSCapSelection];
					}
				}

				if (result != RFU_TRAYMENU_CONSOLE
					&& result != RFU_TRAYMENU_LOADSET
					&& result != RFU_TRAYMENU_GITHUB
					&& result != RFU_TRAYMENU_EXIT)
				{
					Settings::Update();
					Settings::Save();
				}
			}

			return 1;
		}

		break;
	}
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return 0;
}

void UI::CreateHiddenConsole()
{
	AllocConsole();

	freopen("CONOUT$", "w", stdout);
	freopen("CONIN$", "r", stdin);

	if (!UI::IsConsoleOnly)
	{
		HMENU menu = GetSystemMenu(GetConsoleWindow(), FALSE);
		EnableMenuItem(menu, SC_CLOSE, MF_GRAYED);
	}

#ifdef _WIN64
	SetConsoleTitleA("Roblox FPS Unlocker " RFU_VERSION " (64-bit) Console");
#else
	SetConsoleTitleA("Roblox FPS Unlocker " RFU_VERSION " (32-bit) Console");
#endif

	ShowWindow(GetConsoleWindow(), SW_HIDE);
}

bool UI::ToggleConsole()
{
	static bool toggle = false;

	if (!GetConsoleWindow())
		UI::CreateHiddenConsole();

	toggle = !toggle;
	ShowWindow(GetConsoleWindow(), toggle ? SW_SHOWNORMAL : SW_HIDE);

	return toggle;
}

int UI::Start(HINSTANCE instance, LPTHREAD_START_ROUTINE watchthread)
{

	
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(wcex);
	wcex.style = 0;
	wcex.lpfnWndProc = WindowProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = NULL;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = "RFUClass";
	wcex.hIconSm = NULL;

	RegisterClassEx(&wcex);

	UI::Window = CreateWindow("RFUClass", "Roblox FPS Unlocker", 0, 0, 0, 0, 0, NULL, NULL, instance, NULL);
	if (!UI::Window)
		return 0;

	NotifyIconData.cbSize = sizeof(NotifyIconData);
	NotifyIconData.hWnd = UI::Window;
	NotifyIconData.uID = IDI_ICON1;
	NotifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	NotifyIconData.uCallbackMessage = RFU_TRAYICON;
	NotifyIconData.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_ICON1));
	strcpy_s(NotifyIconData.szTip, "Roblox FPS Unlocker");

	Shell_NotifyIcon(NIM_ADD, &NotifyIconData);

	WatchThread = CreateThread(NULL, 0, watchthread, NULL, NULL, NULL);

	BOOL ret;
	MSG msg;

	while ((ret = GetMessage(&msg, NULL, 0, 0)) != 0)
	{
		if (ret == -1)
		{
			
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return msg.wParam;
}