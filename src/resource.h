#pragma once

#define IDI_APP_ICON        101

// Tray menu command IDs
#define IDM_TOGGLE          1001
#define IDM_EDIT_SETTINGS   1002
#define IDM_AUTOSTART       1003
#define IDM_EXIT            1004

// Custom window messages
#define WM_TRAYICON         (WM_APP + 1)
#define WM_CONFIG_CHANGED   (WM_APP + 2)
#define WM_ZORDER_RECHECK   (WM_APP + 3)  // re-assert overlay z-order (posted by ZOrderEventProc)

// Hotkey IDs
#define HOTKEY_TOGGLE       1
#define HOTKEY_TEMP_HIDE    2

// Timer IDs
#define TIMER_REFRESH           1
#define TIMER_HOTKEY_NOTIFY     2   // one-shot: show hotkey-error balloon after startup
#define TIMER_ZORDER            3   // periodic z-order re-assertion (independent of content refresh)
#define TIMER_TEMP_HIDE         4   // auto-restore watermark after temporary hide
#define TIMER_SIMULATE_SCREENSHOT 5 // delayed screenshot hotkey simulation
