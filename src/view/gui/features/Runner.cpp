/*
 * Copyright (c) 2025, Mupen64 maintainers, contributors, and original authors (Hacktarux, ShadowPrince, linker).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Runner.h"

#include <thread>
#include <Windows.h>
#include <Windowsx.h>
#include <view/resource.h>
#include <view/gui/Main.h>
#include <view/lua/LuaConsole.h>
#include <core/Messenger.h>
#include <core/Config.h>
#include <core/r4300/r4300.h>
#include <core/r4300/vcr.h>
#include <core/AsyncExecutor.h>

namespace Runner
{
    int32_t last_selected_id = -1;

    void run_auto(int id, std::filesystem::path path)
    {
        switch (id)
        {
        case IDC_LIST_ROMS:
            AsyncExecutor::invoke_async([path]
            {
                const auto result = vr_start_rom(path);
                show_error_dialog_for_result(result);
            });
            break;
        case IDC_LIST_MOVIES:
            g_config.vcr_readonly = true;
            Messenger::broadcast(
                Messenger::Message::ReadonlyChanged,
                (bool)g_config.vcr_readonly);
            AsyncExecutor::invoke_async([=]
            {
                VCR::start_playback(path);
            });
            break;
        case IDC_LIST_SCRIPTS:
            lua_create_and_run(path);
            break;
        }
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam,
                             LPARAM lParam)
    {
        switch (Message)
        {
        case WM_INITDIALOG:
            {
                last_selected_id = -1;
                auto populate_with_paths = [&](const int id, std::vector<std::wstring> paths)
                {
                    auto ctl = GetDlgItem(hwnd, id);
                    for (auto path : paths)
                    {
                        auto index = ListBox_AddString(ctl, std::filesystem::path(path).filename().wstring().c_str());

                        auto buffer = new wchar_t[path.size() + 1]();
                        memcpy(buffer, path.data(), path.size() * sizeof(wchar_t));

                        ListBox_SetItemData(ctl, index, reinterpret_cast<LPARAM>(buffer));
                    }
                };
                populate_with_paths(IDC_LIST_ROMS, g_config.recent_rom_paths);
                populate_with_paths(IDC_LIST_MOVIES, g_config.recent_movie_paths);
                populate_with_paths(IDC_LIST_SCRIPTS, g_config.recent_lua_script_paths);
                break;
            }
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_LIST_ROMS:
            case IDC_LIST_MOVIES:
            case IDC_LIST_SCRIPTS:
                if (HIWORD(wParam) == LBN_DBLCLK)
                {
                    auto index = ListBox_GetCurSel(GetDlgItem(hwnd, LOWORD(wParam)));

                    if (index == -1)
                    {
                        break;
                    }

                    auto buffer = (char*)ListBox_GetItemData(GetDlgItem(hwnd, LOWORD(wParam)), index);
                    std::filesystem::path path(buffer);
                    delete buffer;

                    EndDialog(hwnd, IDOK);

                    run_auto(LOWORD(wParam), path);
                }
                if (HIWORD(wParam) == LBN_SELCHANGE)
                {
                    // Clear the selections of the other items
                    for (auto id : {IDC_LIST_ROMS, IDC_LIST_MOVIES, IDC_LIST_SCRIPTS})
                    {
                        if (id == LOWORD(wParam))
                        {
                            continue;
                        }
                        ListBox_SetCurSel(GetDlgItem(hwnd, id), -1);
                    }

                    last_selected_id = LOWORD(wParam);
                }
                break;
            case IDOK:
                {
                    if (!last_selected_id)
                    {
                        break;
                    }

                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(last_selected_id, LBN_DBLCLK), 0);
                    break;
                }
            case IDCANCEL:
                EndDialog(hwnd, IDCANCEL);
                break;
            default: break;
            }
            break;
        default: break;
        }
        return FALSE;
    }

    void show()
    {
        DialogBox(g_app_instance,
                  MAKEINTRESOURCE(IDD_RUNNER), g_main_hwnd,
                  (DLGPROC)WndProc);
    }
}
