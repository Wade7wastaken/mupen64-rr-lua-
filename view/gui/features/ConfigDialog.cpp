/***************************************************************************
						  configdialog.c  -  description
							 -------------------
	copyright            : (C) 2003 by ShadowPrince
	email                : shadow@emulation64.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <shlobj.h>
#include <cstdio>
#include <vector>
#include <cassert>
#include <view/lua/LuaConsole.h>
#include <view/gui/Main.h>
#include <winproject/resource.h>
#include <core/r4300/Plugin.hpp>
#include <view/gui/features/RomBrowser.hpp>
#include <core/r4300/timers.h>
#include <shared/Config.hpp>
#include <view/gui/wrapper/PersistentPathDialog.h>
#include <shared/helpers/StringHelpers.h>
#include <view/helpers/WinHelpers.h>
#include <view/helpers/StringHelpers.h>
#include <core/r4300/r4300.h>
#include "configdialog.h"
#include <view/capture/EncodingManager.h>

#include <shared/Messenger.h>
#include <Windows.h>


/**
 * Represents a group of options in the settings.
 */
typedef struct
{
	/**
	 * The group's unique identifier.
	 */
	size_t id;
	
	/**
	 * The group's name.
	 */
	std::wstring name;
	
} t_options_group;

/**
 * Represents a settings option.
 */
typedef struct OptionsItem
{
	enum class Type
	{
		Invalid,
		Bool,
		Number,
		Enum,
	};
	
	/**
	 * The group this option belongs to.
	 */
	size_t group_id = -1;
	
	/**
	 * The option's name.
	 */
	std::string name;

	/**
	 * The option's backing data.
	 */
	int32_t* data = nullptr;
	
	/**
	 * The option's backing data type.
	 */
	Type type = Type::Invalid;
	
	/**
	 * Possible predefined values (e.g.: enum values) for an option along with a name.
	 *
	 * Only applicable when <c>type == Type::Enum<c>.
	 */
	std::vector<std::pair<std::wstring, int32_t>> possible_values = {};

	/**
	 * Function which returns whether the option can be changed. Useful for values which shouldn't be changed during emulation. 
	 */
	std::function<bool()> is_readonly = [] { return false; };

	/**
	 * Gets the value name for the current backing data, or the provided default name if no match is found.
	 */
	std::wstring get_value_name(std::wstring default_name = L"Invalid Value")
	{
		if (type == Type::Bool)
		{
			return *data ? L"Enabled" : L"-";
		}

		if (type == Type::Number)
		{
			return std::to_wstring(*data);
		}
		
		for (auto [name, val] : possible_values)
		{
			if (*data == val)
			{
				return name;
			}
		}
		
		return default_name;
	}
} t_options_item;

std::vector<std::unique_ptr<Plugin>> available_plugins;
std::vector<HWND> tooltips;
std::vector<t_options_group> g_option_groups;
std::vector<t_options_item> g_option_items;
HWND g_lv_hwnd;

/// <summary>
/// Waits until the user inputs a valid key sequence, then fills out the hotkey
/// </summary>
/// <returns>
/// Whether a hotkey has successfully been picked
/// </returns>
int32_t get_user_hotkey(t_hotkey* hotkey)
{
	int i, j;
	int lc = 0, ls = 0, la = 0;
	for (i = 0; i < 500; i++)
	{
		SleepEx(10, TRUE);
		for (j = 8; j < 254; j++)
		{
			if (j == VK_LCONTROL || j == VK_RCONTROL || j == VK_LMENU || j ==
				VK_RMENU || j == VK_LSHIFT || j == VK_RSHIFT)
				continue;

			if (GetAsyncKeyState(j) & 0x8000)
			{
				// HACK to avoid exiting all the way out of the dialog on pressing escape to clear a hotkeys
				// or continually re-activating the button on trying to assign space as a hotkeys
				if (j == VK_ESCAPE)
					return 0;

				if (j == VK_CONTROL)
				{
					lc = 1;
					continue;
				}
				if (j == VK_SHIFT)
				{
					ls = 1;
					continue;
				}
				if (j == VK_MENU)
				{
					la = 1;
					continue;
				}
				if (j != VK_ESCAPE)
				{
					hotkey->key = j;
					hotkey->shift = GetAsyncKeyState(VK_SHIFT) ? 1 : 0;
					hotkey->ctrl = GetAsyncKeyState(VK_CONTROL) ? 1 : 0;
					hotkey->alt = GetAsyncKeyState(VK_MENU) ? 1 : 0;
					return 1;
				}
				memset(hotkey, 0, sizeof(t_hotkey)); // clear key on escape
				return 0;
			}
			if (j == VK_CONTROL && lc)
			{
				hotkey->key = 0;
				hotkey->shift = 0;
				hotkey->ctrl = 1;
				hotkey->alt = 0;
				return 1;
			}
			if (j == VK_SHIFT && ls)
			{
				hotkey->key = 0;
				hotkey->shift = 1;
				hotkey->ctrl = 0;
				hotkey->alt = 0;
				return 1;
			}
			if (j == VK_MENU && la)
			{
				hotkey->key = 0;
				hotkey->shift = 0;
				hotkey->ctrl = 0;
				hotkey->alt = 1;
				return 1;
			}
		}
	}
	//we checked all keys and none of them was pressed, so give up
	return 0;
}

BOOL CALLBACK about_dlg_proc(const HWND hwnd, const UINT message, const WPARAM w_param, LPARAM)
{
	switch (message)
	{
	case WM_INITDIALOG:
		SendDlgItemMessage(hwnd, IDB_LOGO, STM_SETIMAGE, IMAGE_BITMAP,
		                   (LPARAM)LoadImage(g_app_instance, MAKEINTRESOURCE(IDB_LOGO),
		                                     IMAGE_BITMAP, 0, 0, 0));
		return TRUE;

	case WM_CLOSE:
		EndDialog(hwnd, IDOK);
		break;

	case WM_COMMAND:
		switch (LOWORD(w_param))
		{
		case IDOK:
			EndDialog(hwnd, IDOK);
			break;

		case IDC_WEBSITE:
			ShellExecute(nullptr, nullptr, "http://mupen64.emulation64.com", nullptr, nullptr, SW_SHOW);
			break;
		case IDC_GITREPO:
			ShellExecute(nullptr, nullptr, "https://github.com/mkdasher/mupen64-rr-lua-/", nullptr, nullptr, SW_SHOW);
			break;
		default:
			break;
		}
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

void configdialog_about()
{
	DialogBox(g_app_instance,
	          MAKEINTRESOURCE(IDD_ABOUT), g_main_hwnd,
	          about_dlg_proc);
}

void build_rom_browser_path_list(const HWND dialog_hwnd)
{
	const HWND hwnd = GetDlgItem(dialog_hwnd, IDC_ROMBROWSER_DIR_LIST);

	SendMessage(hwnd, LB_RESETCONTENT, 0, 0);

	for (const std::string& str : g_config.rombrowser_rom_paths)
	{
		SendMessage(hwnd, LB_ADDSTRING, 0,
		            (LPARAM)str.c_str());
	}
}

BOOL CALLBACK directories_cfg(const HWND hwnd, const UINT message, const WPARAM w_param, LPARAM l_param)
{
	[[maybe_unused]] LPITEMIDLIST pidl{};
	BROWSEINFO bi{};
	auto l_nmhdr = (NMHDR*)&l_param;
	switch (message)
	{
	case WM_INITDIALOG:

		build_rom_browser_path_list(hwnd);

		SendMessage(GetDlgItem(hwnd, IDC_RECURSION), BM_SETCHECK,
		            g_config.is_rombrowser_recursion_enabled ? BST_CHECKED : BST_UNCHECKED, 0);

		if (g_config.is_default_plugins_directory_used)
		{
			SendMessage(GetDlgItem(hwnd, IDC_DEFAULT_PLUGINS_CHECK), BM_SETCHECK, BST_CHECKED, 0);
			EnableWindow(GetDlgItem(hwnd, IDC_PLUGINS_DIR), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_PLUGINS_DIR), FALSE);
		}
		if (g_config.is_default_saves_directory_used)
		{
			SendMessage(GetDlgItem(hwnd, IDC_DEFAULT_SAVES_CHECK), BM_SETCHECK, BST_CHECKED, 0);
			EnableWindow(GetDlgItem(hwnd, IDC_SAVES_DIR), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_SAVES_DIR), FALSE);
		}
		if (g_config.is_default_screenshots_directory_used)
		{
			SendMessage(GetDlgItem(hwnd, IDC_DEFAULT_SCREENSHOTS_CHECK), BM_SETCHECK, BST_CHECKED, 0);
			EnableWindow(GetDlgItem(hwnd, IDC_SCREENSHOTS_DIR), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_SCREENSHOTS_DIR), FALSE);
		}
		if (g_config.is_default_backups_directory_used)
		{
			SendMessage(GetDlgItem(hwnd, IDC_DEFAULT_BACKUPS_CHECK), BM_SETCHECK, BST_CHECKED, 0);
			EnableWindow(GetDlgItem(hwnd, IDC_BACKUPS_DIR), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_BACKUPS_DIR), FALSE);
		}

		SetDlgItemText(hwnd, IDC_PLUGINS_DIR, g_config.plugins_directory.c_str());
		SetDlgItemText(hwnd, IDC_SAVES_DIR, g_config.saves_directory.c_str());
		SetDlgItemText(hwnd, IDC_SCREENSHOTS_DIR, g_config.screenshots_directory.c_str());
		SetDlgItemText(hwnd, IDC_BACKUPS_DIR, g_config.backups_directory.c_str());

		if (emu_launched)
		{
			EnableWindow(GetDlgItem(hwnd, IDC_DEFAULT_SAVES_CHECK), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SAVES_DIR), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_SAVES_DIR), FALSE);
		}
		break;
	case WM_NOTIFY:
		if (l_nmhdr->code == PSN_APPLY)
		{
			char str[MAX_PATH] = {0};
			int selected = SendDlgItemMessage(hwnd, IDC_DEFAULT_PLUGINS_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED
				               ? TRUE
				               : FALSE;
			GetDlgItemText(hwnd, IDC_PLUGINS_DIR, str, 200);
			g_config.plugins_directory = std::string(str);
			g_config.is_default_plugins_directory_used = selected;

			selected = SendDlgItemMessage(hwnd, IDC_DEFAULT_SAVES_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED
				           ? TRUE
				           : FALSE;
			GetDlgItemText(hwnd, IDC_SAVES_DIR, str, MAX_PATH);
			g_config.saves_directory = std::string(str);
			g_config.is_default_saves_directory_used = selected;

			selected = SendDlgItemMessage(hwnd, IDC_DEFAULT_SCREENSHOTS_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED
				           ? TRUE
				           : FALSE;
			GetDlgItemText(hwnd, IDC_SCREENSHOTS_DIR, str, MAX_PATH);
			g_config.screenshots_directory = std::string(str);
			g_config.is_default_screenshots_directory_used = selected;

			selected = SendDlgItemMessage(hwnd, IDC_DEFAULT_BACKUPS_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED
						   ? TRUE
						   : FALSE;
			GetDlgItemText(hwnd, IDC_BACKUPS_DIR, str, MAX_PATH);
			g_config.backups_directory = std::string(str);
			g_config.is_default_backups_directory_used = selected;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(w_param))
		{
		case IDC_RECURSION:
			g_config.is_rombrowser_recursion_enabled = SendDlgItemMessage(hwnd, IDC_RECURSION, BM_GETCHECK, 0, 0) ==
				BST_CHECKED;
			break;
		case IDC_ADD_BROWSER_DIR:
			{
				const auto path = show_persistent_folder_dialog("f_roms", hwnd);
				if (path.empty())
				{
					break;
				}
				g_config.rombrowser_rom_paths.push_back(wstring_to_string(path));
				build_rom_browser_path_list(hwnd);
				break;
			}
		case IDC_REMOVE_BROWSER_DIR:
			{
				if (const int32_t selected_index = SendMessage(GetDlgItem(hwnd, IDC_ROMBROWSER_DIR_LIST), LB_GETCURSEL, 0, 0); selected_index != -1)
				{
					g_config.rombrowser_rom_paths.erase(g_config.rombrowser_rom_paths.begin() + selected_index);
				}
				build_rom_browser_path_list(hwnd);
				break;
			}

		case IDC_REMOVE_BROWSER_ALL:
			g_config.rombrowser_rom_paths.clear();
			build_rom_browser_path_list(hwnd);
			break;

		case IDC_DEFAULT_PLUGINS_CHECK:
			{
				g_config.is_default_plugins_directory_used = SendMessage(GetDlgItem(hwnd, IDC_DEFAULT_PLUGINS_CHECK),
				                                                       BM_GETCHECK, 0, 0);
				EnableWindow(GetDlgItem(hwnd, IDC_PLUGINS_DIR), !g_config.is_default_plugins_directory_used);
				EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_PLUGINS_DIR), !g_config.is_default_plugins_directory_used);
			}
			break;
		case IDC_DEFAULT_BACKUPS_CHECK:
			{
				g_config.is_default_backups_directory_used = SendMessage(GetDlgItem(hwnd, IDC_DEFAULT_BACKUPS_CHECK),
																	   BM_GETCHECK, 0, 0);
				EnableWindow(GetDlgItem(hwnd, IDC_BACKUPS_DIR), !g_config.is_default_backups_directory_used);
				EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_BACKUPS_DIR), !g_config.is_default_backups_directory_used);
			}
			break;
		case IDC_PLUGIN_DIRECTORY_HELP:
			{
				MessageBox(hwnd, "Changing the plugin directory may introduce bugs to some plugins.", "Info",
				           MB_ICONINFORMATION | MB_OK);
			}
			break;
		case IDC_CHOOSE_PLUGINS_DIR:
			{
				const auto path = show_persistent_folder_dialog("f_plugins", hwnd);
				if (path.empty())
				{
					break;
				}
				g_config.plugins_directory = wstring_to_string(path) + "\\";
				SetDlgItemText(hwnd, IDC_PLUGINS_DIR, g_config.plugins_directory.c_str());
			}
			break;
		case IDC_DEFAULT_SAVES_CHECK:
			{
				g_config.is_default_saves_directory_used = SendMessage(GetDlgItem(hwnd, IDC_DEFAULT_SAVES_CHECK),
				                                                     BM_GETCHECK, 0, 0);
				EnableWindow(GetDlgItem(hwnd, IDC_SAVES_DIR), !g_config.is_default_saves_directory_used);
				EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_SAVES_DIR), !g_config.is_default_saves_directory_used);
			}
			break;
		case IDC_CHOOSE_SAVES_DIR:
			{
				const auto path = show_persistent_folder_dialog("f_saves", hwnd);
				if (path.empty())
				{
					break;
				}
				g_config.saves_directory = wstring_to_string(path) + "\\";
				SetDlgItemText(hwnd, IDC_SAVES_DIR, g_config.saves_directory.c_str());
			}
			break;
		case IDC_DEFAULT_SCREENSHOTS_CHECK:
			{
				g_config.is_default_screenshots_directory_used = SendMessage(
					GetDlgItem(hwnd, IDC_DEFAULT_SCREENSHOTS_CHECK), BM_GETCHECK, 0, 0);
				EnableWindow(GetDlgItem(hwnd, IDC_SCREENSHOTS_DIR), !g_config.is_default_screenshots_directory_used);
				EnableWindow(GetDlgItem(hwnd, IDC_CHOOSE_SCREENSHOTS_DIR),
				             !g_config.is_default_screenshots_directory_used);
			}
			break;
		case IDC_CHOOSE_SCREENSHOTS_DIR:
			{
				const auto path = show_persistent_folder_dialog("f_screenshots", hwnd);
				if (path.empty())
				{
					break;
				}
				g_config.screenshots_directory = wstring_to_string(path) + "\\";
				SetDlgItemText(hwnd, IDC_SCREENSHOTS_DIR, g_config.screenshots_directory.c_str());
			}
			break;
		case IDC_CHOOSE_BACKUPS_DIR:
			{
				const auto path = show_persistent_folder_dialog("f_backups", hwnd);
				if (path.empty())
				{
					break;
				}
				g_config.backups_directory = wstring_to_string(path) + "\\";
				SetDlgItemText(hwnd, IDC_BACKUPS_DIR, g_config.backups_directory.c_str());
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return FALSE;
}

void update_plugin_selection(const HWND hwnd, const int32_t id, const std::filesystem::path& path)
{
	for (int i = 0; i < SendDlgItemMessage(hwnd, id, CB_GETCOUNT, 0, 0); ++i)
	{
		if (const auto plugin = (Plugin*)SendDlgItemMessage(hwnd, id, CB_GETITEMDATA, i, 0); plugin->path() == path)
		{
			ComboBox_SetCurSel(GetDlgItem(hwnd, id), i);
			break;
		}
	}
	SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
}

Plugin* get_selected_plugin(const HWND hwnd, const int id)
{
	const int i = SendDlgItemMessage(hwnd, id, CB_GETCURSEL, 0, 0);
	const auto res = SendDlgItemMessage(hwnd, id, CB_GETITEMDATA, i, 0);
	return res == CB_ERR ? nullptr : (Plugin*)res;
}

BOOL CALLBACK plugins_cfg(const HWND hwnd, const UINT message, const WPARAM w_param, const LPARAM l_param)
{
	[[maybe_unused]] char path_buffer[_MAX_PATH];
	NMHDR FAR* l_nmhdr = nullptr;
	memcpy(&l_nmhdr, &l_param, sizeof(NMHDR FAR*));
	switch (message)
	{
	case WM_CLOSE:
		EndDialog(hwnd, IDOK);
		break;
	case WM_INITDIALOG:
		{
			available_plugins = get_available_plugins();

			for (const auto& plugin : available_plugins)
			{
				int32_t id = 0;
				switch (plugin->type())
				{
				case plugin_type::video:
					id = IDC_COMBO_GFX;
					break;
				case plugin_type::audio:
					id = IDC_COMBO_SOUND;
					break;
				case plugin_type::input:
					id = IDC_COMBO_INPUT;
					break;
				case plugin_type::rsp:
					id = IDC_COMBO_RSP;
					break;
				default:
					assert(false);
					break;
				}
				// we add the string and associate a pointer to the plugin with the item
				const int i = SendDlgItemMessage(hwnd, id, CB_GETCOUNT, 0, 0);
				SendDlgItemMessage(hwnd, id, CB_ADDSTRING, 0, (LPARAM)plugin->name().c_str());
				SendDlgItemMessage(hwnd, id, CB_SETITEMDATA, i, (LPARAM)plugin.get());
			}

			update_plugin_selection(hwnd, IDC_COMBO_GFX, g_config.selected_video_plugin);
			update_plugin_selection(hwnd, IDC_COMBO_SOUND, g_config.selected_audio_plugin);
			update_plugin_selection(hwnd, IDC_COMBO_INPUT, g_config.selected_input_plugin);
			update_plugin_selection(hwnd, IDC_COMBO_RSP, g_config.selected_rsp_plugin);

			EnableWindow(GetDlgItem(hwnd, IDC_COMBO_GFX), !emu_launched);
			EnableWindow(GetDlgItem(hwnd, IDC_COMBO_INPUT), !emu_launched);
			EnableWindow(GetDlgItem(hwnd, IDC_COMBO_SOUND), !emu_launched);
			EnableWindow(GetDlgItem(hwnd, IDC_COMBO_RSP), !emu_launched);

			SendDlgItemMessage(hwnd, IDB_DISPLAY, STM_SETIMAGE, IMAGE_BITMAP,
			                   (LPARAM)LoadImage(g_app_instance, MAKEINTRESOURCE(IDB_DISPLAY),
			                                     IMAGE_BITMAP, 0, 0, 0));
			SendDlgItemMessage(hwnd, IDB_CONTROL, STM_SETIMAGE, IMAGE_BITMAP,
			                   (LPARAM)LoadImage(g_app_instance, MAKEINTRESOURCE(IDB_CONTROL),
			                                     IMAGE_BITMAP, 0, 0, 0));
			SendDlgItemMessage(hwnd, IDB_SOUND, STM_SETIMAGE, IMAGE_BITMAP,
			                   (LPARAM)LoadImage(g_app_instance, MAKEINTRESOURCE(IDB_SOUND),
			                                     IMAGE_BITMAP, 0, 0, 0));
			SendDlgItemMessage(hwnd, IDB_RSP, STM_SETIMAGE, IMAGE_BITMAP,
			                   (LPARAM)LoadImage(g_app_instance, MAKEINTRESOURCE(IDB_RSP),
			                                     IMAGE_BITMAP, 0, 0, 0));
			return TRUE;
		}
	case WM_COMMAND:
		switch (LOWORD(w_param))
		{
		case IDC_COMBO_GFX:
		case IDC_COMBO_SOUND:
		case IDC_COMBO_INPUT:
		case IDC_COMBO_RSP:
			{
				auto has_plugin_selected =
					ComboBox_GetItemData(GetDlgItem(hwnd, LOWORD(w_param)), ComboBox_GetCurSel(GetDlgItem(hwnd, LOWORD(w_param))))
					&& ComboBox_GetCurSel(GetDlgItem(hwnd, LOWORD(w_param))) != CB_ERR;

				switch (LOWORD(w_param))
				{
				case IDC_COMBO_GFX:
					EnableWindow(GetDlgItem(hwnd, IDM_VIDEO_SETTINGS), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDGFXTEST), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDGFXABOUT), has_plugin_selected);
					break;
				case IDC_COMBO_SOUND:
					EnableWindow(GetDlgItem(hwnd, IDM_AUDIO_SETTINGS), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDSOUNDTEST), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDSOUNDABOUT), has_plugin_selected);
					break;
				case IDC_COMBO_INPUT:
					EnableWindow(GetDlgItem(hwnd, IDM_INPUT_SETTINGS), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDINPUTTEST), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDINPUTABOUT), has_plugin_selected);
					break;
				case IDC_COMBO_RSP:
					EnableWindow(GetDlgItem(hwnd, IDM_RSP_SETTINGS), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDRSPTEST), has_plugin_selected);
					EnableWindow(GetDlgItem(hwnd, IDRSPABOUT), has_plugin_selected);
					break;
				default:
					break;
				}
				break;
			}
		case IDM_VIDEO_SETTINGS:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_GFX)->config();
			break;
		case IDGFXTEST:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_GFX)->test();
			break;
		case IDGFXABOUT:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_GFX)->about();
			break;
		case IDM_INPUT_SETTINGS:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_INPUT)->config();
			break;
		case IDINPUTTEST:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_INPUT)->test();
			break;
		case IDINPUTABOUT:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_INPUT)->about();
			break;
		case IDM_AUDIO_SETTINGS:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_SOUND)->config();
			break;
		case IDSOUNDTEST:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_SOUND)->test();
			break;
		case IDSOUNDABOUT:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_SOUND)->about();
			break;
		case IDM_RSP_SETTINGS:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_RSP)->config();
			break;
		case IDRSPTEST:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_RSP)->test();
			break;
		case IDRSPABOUT:
			g_hwnd_plug = hwnd;
			get_selected_plugin(hwnd, IDC_COMBO_RSP)->about();
			break;
		default:
			break;
		}
		break;
	case WM_NOTIFY:
		if (l_nmhdr->code == PSN_APPLY)
		{
			if (const auto plugin = get_selected_plugin(hwnd, IDC_COMBO_GFX); plugin != nullptr)
			{
				g_config.selected_video_plugin = plugin->path().string();
			}
			if (const auto plugin = get_selected_plugin(hwnd, IDC_COMBO_SOUND); plugin != nullptr)
			{
				g_config.selected_audio_plugin = plugin->path().string();
			}
			if (const auto plugin = get_selected_plugin(hwnd, IDC_COMBO_INPUT); plugin != nullptr)
			{
				g_config.selected_input_plugin = plugin->path().string();
			}
			if (const auto plugin = get_selected_plugin(hwnd, IDC_COMBO_RSP); plugin != nullptr)
			{
				g_config.selected_rsp_plugin = plugin->path().string();
			}
		}
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

void get_config_listview_items(std::vector<t_options_group>& groups, std::vector<t_options_item>& options)
{
	t_options_group interface_group = {
		.id = 1,
		.name = L"Interface"
	};

	t_options_group flow_group = {
		.id = 2,
		.name = L"Flow"
	};

	t_options_group capture_group = {
		.id = 3,
		.name = L"Capture"
	};

	t_options_group core_group = {
		.id = 4,
		.name = L"Core"
	};

	groups = {interface_group, flow_group, capture_group, core_group};

	options = {
		t_options_item {
			.group_id = interface_group.id,
			.name = "Statusbar 0-index",
			.data = &g_config.vcr_0_index,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = interface_group.id,
			.name = "Pause when unfocused",
			.data = &g_config.is_unfocused_pause_enabled,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = interface_group.id,
			.name = "Lua Presenter",
			.data = &g_config.presenter_type,
			.type = t_options_item::Type::Enum,
			.possible_values = {
				std::make_pair(L"DirectComposition", (int32_t)PresenterType::DirectComposition),
				std::make_pair(L"GDI", (int32_t)PresenterType::GDI),
			},
			.is_readonly = []
			{
				return !hwnd_lua_map.empty();
			},
		},

		t_options_item {
			.group_id = capture_group.id,
			.name = "Capture delay",
			.data = &g_config.capture_delay,
			.type = t_options_item::Type::Number,
		},
		t_options_item {
			.group_id = capture_group.id,
			.name = "Capture mode",
			.data = &g_config.capture_mode,
			.type = t_options_item::Type::Enum,
			.possible_values = {
				std::make_pair(L"Plugin", 0),
				std::make_pair(L"Window", 1),
				std::make_pair(L"Screen", 2),
				std::make_pair(L"Hybrid", 3),
			},
			.is_readonly = []
			{
				return EncodingManager::is_capturing();
			},
		},
		t_options_item {
			.group_id = capture_group.id,
			.name = "Capture sync",
			.data = &g_config.synchronization_mode,
			.type = t_options_item::Type::Enum,
			.possible_values = {
				std::make_pair(L"None", 0),
				std::make_pair(L"Audio", 1),
				std::make_pair(L"Video", 2),
			},
			.is_readonly = []
			{
				return EncodingManager::is_capturing();
			},
		},

		t_options_item {
			.group_id = core_group.id,
			.name = "Type",
			.data = &g_config.core_type,
			.type = t_options_item::Type::Enum,
			.possible_values = {
				std::make_pair(L"Interpreter", 0),
				std::make_pair(L"Dynamic Recompiler", 1),
				std::make_pair(L"Pure Interpreter", 2),
			},
			.is_readonly = []
			{
				return emu_launched;
			},
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "CPU Clock Speed Multiplier",
			.data = &g_config.cpu_clock_speed_multiplier,
			.type = t_options_item::Type::Number,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Max Lag Frames",
			.data = &g_config.max_lag,
			.type = t_options_item::Type::Number,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Audio Delay",
			.data = &g_config.is_audio_delay_enabled,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Compiled Jump",
			.data = &g_config.is_compiled_jump_enabled,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "WiiVC Mode",
			.data = &g_config.wii_vc_emulation,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Auto-increment Slot",
			.data = &g_config.increment_slot,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Emulate Float Crashes",
			.data = &g_config.is_float_exception_propagation_enabled,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Movie Backups",
			.data = &g_config.vcr_backups,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Fast-Forward Skip Frequency",
			.data = &g_config.frame_skip_frequency,
			.type = t_options_item::Type::Number,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Record Resets",
			.data = &g_config.is_reset_recording_enabled,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Emulate SD Card",
			.data = &g_config.is_reset_recording_enabled,
			.type = t_options_item::Type::Bool,
		},
		t_options_item {
			.group_id = core_group.id,
			.name = "Instant Savestate Update",
			.data = &g_config.st_screenshot,
			.type = t_options_item::Type::Bool,
		},
	};
}

BOOL CALLBACK general_cfg(const HWND hwnd, const UINT message, const WPARAM w_param, const LPARAM l_param)
{
	NMHDR FAR* l_nmhdr = nullptr;
	memcpy(&l_nmhdr, &l_param, sizeof(NMHDR FAR*));

	switch (message)
	{
	case WM_INITDIALOG:
		{
			if (g_lv_hwnd)
			{
				DestroyWindow(g_lv_hwnd);
			}

			g_option_groups.clear();
			g_option_items.clear();
			get_config_listview_items(g_option_groups, g_option_items);
			
			RECT grid_rect {};
			GetClientRect(hwnd, &grid_rect);
			
			g_lv_hwnd = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
										   WS_TABSTOP | WS_VISIBLE | WS_CHILD |
										   LVS_SINGLESEL | LVS_REPORT |
										   LVS_SHOWSELALWAYS | LVS_ALIGNTOP,
										   grid_rect.left, grid_rect.top,
										   grid_rect.right - grid_rect.left,
										   grid_rect.bottom - grid_rect.top,
										   hwnd, (HMENU)IDC_SETTINGS_LV,
										   g_app_instance,
										   NULL);

			ListView_EnableGroupView(g_lv_hwnd, true);
			ListView_SetExtendedListViewStyle(g_lv_hwnd,
											  LVS_EX_FULLROWSELECT |
											  LVS_EX_DOUBLEBUFFER);

			for (auto group : g_option_groups)
			{
				LVGROUP lvgroup;
				lvgroup.cbSize    = sizeof(LVGROUP);
				lvgroup.mask      = LVGF_HEADER | LVGF_GROUPID;
				// FIXME: This is concerning, but seems to work
				lvgroup.pszHeader = const_cast<wchar_t*>(group.name.c_str());
				lvgroup.iGroupId  = group.id;
				ListView_InsertGroup(g_lv_hwnd, -1, &lvgroup);
			}
			
			LVCOLUMN lv_column = {0};
			lv_column.mask = LVCF_FMT | LVCF_DEFAULTWIDTH | LVCF_TEXT | LVCF_SUBITEM;

			lv_column.pszText = (LPTSTR)"Name";
			ListView_InsertColumn(g_lv_hwnd, 0, &lv_column);
			lv_column.pszText = (LPTSTR)"Value";
			ListView_InsertColumn(g_lv_hwnd, 1, &lv_column);
			lv_column.pszText = (LPTSTR)"Description";
			ListView_InsertColumn(g_lv_hwnd, 2, &lv_column);
			
			LVITEM lv_item = {0};
			lv_item.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_PARAM;
			lv_item.pszText = LPSTR_TEXTCALLBACK;
			for (int i = 0; i < g_option_items.size(); ++i)
			{
				lv_item.iGroupId = g_option_items[i].group_id;
				lv_item.lParam = i;
				lv_item.iItem = i;
				ListView_InsertItem(g_lv_hwnd, &lv_item);
			}
			
			ListView_SetColumnWidth(g_lv_hwnd, 0, LVSCW_AUTOSIZE_USEHEADER);
			ListView_SetColumnWidth(g_lv_hwnd, 1, LVSCW_AUTOSIZE_USEHEADER);
			ListView_SetColumnWidth(g_lv_hwnd, 2, LVSCW_AUTOSIZE_USEHEADER);
			
			return TRUE;
		}

	case WM_COMMAND:
		switch (LOWORD(w_param))
		{
		case IDC_SKIPFREQUENCY_HELP:
			MessageBox(hwnd, "0 = Skip all frames, 1 = Show all frames, n = show every nth frame", "Info",
			           MB_OK | MB_ICONINFORMATION);
			break;
		case IDC_COMBO_CLOCK_SPD_MULT:
			{
				char buf[260] = {0};
				read_combo_box_value(hwnd, IDC_COMBO_CLOCK_SPD_MULT, buf);
				g_config.cpu_clock_speed_multiplier = atoi(&buf[0]);
				break;
			}
		case IDC_ENCODE_MODE:
			{
				g_config.capture_mode = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_ENCODE_MODE));
				break;
			}
		case IDC_ENCODE_SYNC:
			{
				g_config.synchronization_mode = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_ENCODE_SYNC));
				break;
			}
	case IDC_COMBO_LUA_PRESENTER:
			{
				g_config.presenter_type = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_COMBO_LUA_PRESENTER));
				break;
			}
		case IDC_INTERP:
			if (!emu_launched)
			{
				g_config.core_type = 0;
			}
			break;
		case IDC_RECOMP:
			if (!emu_launched)
			{
				g_config.core_type = 1;
			}
			break;
		case IDC_PURE_INTERP:
			if (!emu_launched)
			{
				g_config.core_type = 2;
			}
			break;
		default:
			break;
		}
		break;

	case WM_NOTIFY:
		{
			if (w_param == IDC_SETTINGS_LV)
			{
				switch (((LPNMHDR)l_param)->code)
				{
				case LVN_GETDISPINFO:
					{
						auto plvdi = (NMLVDISPINFO*)l_param;
						t_options_item options_item = g_option_items[plvdi->item.lParam];
						switch (plvdi->item.iSubItem)
						{
						case 0:
							strcpy(plvdi->item.pszText, options_item.name.c_str());
							break;
						case 1:
							strcpy(plvdi->item.pszText, wstring_to_string(options_item.get_value_name()).c_str());
							break;
						}
						break;
					}
				case NM_DBLCLK:
					{
						int32_t i = ListView_GetNextItem(g_lv_hwnd, -1, LVNI_SELECTED);

						if (i == -1) break;

						LVITEM item = {0};
						item.mask = LVIF_PARAM;
						item.iItem = i;
						ListView_GetItem(g_lv_hwnd, &item);
						
						auto option_item = g_option_items[item.lParam];

						// TODO: Perhaps gray out readonly values too?
						if (option_item.is_readonly())
						{
							break;
						}
						
						// For bools, just flip the value...
						if (option_item.type == OptionsItem::Type::Bool)
						{
							*option_item.data ^= true;
							goto update;
						}

						// For enums, cycle through the possible values
						if (option_item.type == OptionsItem::Type::Enum)
						{

							// 1. Find the index of the currently selected item, while falling back to the first possible value if there's no match
							size_t current_value = option_item.possible_values[0].second;
							for (auto possible_value : option_item.possible_values | std::views::values)
							{
								if(*option_item.data == possible_value)
								{
									current_value = possible_value;
									break;
								}
							}

							// 2. Find the lowest and highest values in the vector
							int32_t min_possible_value = INT32_MAX;
							int32_t max_possible_value = INT32_MIN;
							for (const auto& val : option_item.possible_values | std::views::values)
							{
								if (val > max_possible_value)
								{
									max_possible_value = val;
								}
								if (val < min_possible_value)
								{
									min_possible_value = val;
								}
							}

							// 2. Bump it, wrapping around if needed
							current_value++;
							if (current_value > max_possible_value)
							{
								current_value = min_possible_value;
							}

							// 3. Apply the change
							*option_item.data = current_value;
							
							goto update;
						}
						
						update:
						ListView_Update(g_lv_hwnd, i);
					}
					break;
				default:
					break;
				}
			}
			break;
		}
	case WM_DESTROY:
		{
			for (auto tooltip : tooltips)
			{
				DestroyWindow(tooltip);
			}
			tooltips.clear();
			break;
		}
	default:
		return FALSE;
	}
	return TRUE;
}

std::string hotkey_to_string_overview(t_hotkey* hotkey)
{
	return hotkey->identifier + " (" + hotkey_to_string(hotkey) + ")";
}

void on_hotkey_selection_changed(const HWND dialog_hwnd)
{
	HWND list_hwnd = GetDlgItem(dialog_hwnd, IDC_HOTKEY_LIST);
	HWND enable_hwnd = GetDlgItem(dialog_hwnd, IDC_HOTKEY_CLEAR);
	HWND edit_hwnd = GetDlgItem(dialog_hwnd, IDC_SELECTED_HOTKEY_TEXT);
	HWND assign_hwnd = GetDlgItem(dialog_hwnd, IDC_HOTKEY_ASSIGN_SELECTED);

	char selected_identifier[MAX_PATH] = {0};
	SendMessage(list_hwnd, LB_GETTEXT, SendMessage(list_hwnd, LB_GETCURSEL, 0, 0), (LPARAM)selected_identifier);

	int32_t selected_index = ListBox_GetCurSel(list_hwnd);

	EnableWindow(enable_hwnd, selected_index != -1);
	EnableWindow(edit_hwnd, selected_index != -1);
	EnableWindow(assign_hwnd, selected_index != -1);

	SetWindowText(assign_hwnd, "Assign...");

	if (selected_index == -1)
	{
		SetWindowText(edit_hwnd, "");
		return;
	}

	auto hotkey = (t_hotkey*)ListBox_GetItemData(list_hwnd, selected_index);
	SetWindowText(edit_hwnd, hotkey_to_string(hotkey).c_str());
}

void build_hotkey_list(HWND hwnd)
{
	HWND list_hwnd = GetDlgItem(hwnd, IDC_HOTKEY_LIST);

	char search_query_c[MAX_PATH] = {0};
	GetDlgItemText(hwnd, IDC_HOTKEY_SEARCH, search_query_c, std::size(search_query_c));
	std::string search_query = search_query_c;

	ListBox_ResetContent(list_hwnd);

	for (t_hotkey* hotkey : g_config_hotkeys)
	{
		std::string hotkey_string = hotkey_to_string_overview(hotkey);

		if (!search_query.empty())
		{
			if (!contains(to_lower(hotkey_string), to_lower(search_query))
				&& !contains(to_lower(hotkey->identifier), to_lower(search_query)))
			{
				continue;
			}
		}

		int32_t index = ListBox_GetCount(list_hwnd);
		ListBox_AddString(list_hwnd, hotkey_string.c_str());
		ListBox_SetItemData(list_hwnd, index, hotkey);
	}
}

BOOL CALLBACK hotkeys_proc(const HWND hwnd, const UINT message, const WPARAM w_param, LPARAM)
{
	switch (message)
	{
	case WM_INITDIALOG:
		{
			SetDlgItemText(hwnd, IDC_HOTKEY_SEARCH, "");
			on_hotkey_selection_changed(hwnd);
			return TRUE;
		}
	case WM_COMMAND:
		{
			HWND list_hwnd = GetDlgItem(hwnd, IDC_HOTKEY_LIST);
			const int event = HIWORD(w_param);
			const int id = LOWORD(w_param);
			switch (id)
			{
			case IDC_HOTKEY_LIST:
				if (event == LBN_SELCHANGE)
				{
					on_hotkey_selection_changed(hwnd);
				}
				if (event == LBN_DBLCLK)
				{
					on_hotkey_selection_changed(hwnd);
					SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_HOTKEY_ASSIGN_SELECTED, BN_CLICKED), (LPARAM)GetDlgItem(hwnd, IDC_HOTKEY_ASSIGN_SELECTED));
				}
				break;
			case IDC_HOTKEY_ASSIGN_SELECTED:
				{
					int32_t index = ListBox_GetCurSel(list_hwnd);
					if (index == -1)
					{
						break;
					}
					auto hotkey = (t_hotkey*)ListBox_GetItemData(list_hwnd, index);
					SetDlgItemText(hwnd, id, "...");
					get_user_hotkey(hotkey);

					build_hotkey_list(hwnd);
					ListBox_SetCurSel(list_hwnd, (index + 1) % ListBox_GetCount(list_hwnd));
					on_hotkey_selection_changed(hwnd);
				}
				break;
			case IDC_HOTKEY_SEARCH:
				{
					build_hotkey_list(hwnd);
				}
				break;
			case IDC_HOTKEY_CLEAR:
				{
					int32_t index = ListBox_GetCurSel(list_hwnd);
					if (index == -1)
					{
						break;
					}
					auto hotkey = (t_hotkey*)ListBox_GetItemData(list_hwnd, index);
					hotkey->key = 0;
					hotkey->ctrl = 0;
					hotkey->shift = 0;
					hotkey->alt = 0;
					build_hotkey_list(hwnd);
					on_hotkey_selection_changed(hwnd);
				}
				break;
			}
		}
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

void configdialog_show()
{
	PROPSHEETPAGE psp[4] = {{0}};
	for (auto& i : psp)
	{
		i.dwSize = sizeof(PROPSHEETPAGE);
		i.dwFlags = PSP_USETITLE;
		i.hInstance = g_app_instance;
	}

	psp[0].pszTemplate = MAKEINTRESOURCE(IDD_MAIN);
	psp[0].pfnDlgProc = plugins_cfg;
	psp[0].pszTitle = "Plugins";

	psp[1].pszTemplate = MAKEINTRESOURCE(IDD_DIRECTORIES);
	psp[1].pfnDlgProc = directories_cfg;
	psp[1].pszTitle = "Directories";

	psp[2].pszTemplate = MAKEINTRESOURCE(IDD_MESSAGES);
	psp[2].pfnDlgProc = general_cfg;
	psp[2].pszTitle = "General";

	psp[3].pszTemplate = MAKEINTRESOURCE(IDD_NEW_HOTKEY_DIALOG);
	psp[3].pfnDlgProc = hotkeys_proc;
	psp[3].pszTitle = "Hotkeys";

	PROPSHEETHEADER psh = {0};
	psh.dwSize = sizeof(PROPSHEETHEADER);
	psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;
	psh.hwndParent = g_main_hwnd;
	psh.hInstance = g_app_instance;
	psh.pszCaption = "Settings";
	psh.nPages = sizeof(psp) / sizeof(PROPSHEETPAGE);
	psh.ppsp = (LPCPROPSHEETPAGE)&psp;

	const t_config old_config = g_config;

	if (!PropertySheet(&psh))
	{
		g_config = old_config;
	}

	save_config();
	Messenger::broadcast(Messenger::Message::ConfigLoaded, nullptr);
}
