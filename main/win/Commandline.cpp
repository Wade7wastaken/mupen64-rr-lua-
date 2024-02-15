/***************************************************************************
						  commandline.c  -  description
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

// Based on code from 1964 by Schibo and Rice
// Slightly improved command line params parsing function to work with spaced arguments
#include <Windows.h>
#include "Commandline.h"

#include <iostream>

#include "LuaConsole.h"
#include "main_win.h"
#include "messenger.h"
#include "savestates.h"
#include "vcr.h"
#include "helpers/string_helpers.h"
#include "lib/argh.h"

std::string commandline_rom;
std::string commandline_lua;
std::string commandline_st;
std::string commandline_movie;
std::string commandline_avi;
bool commandline_stop_capture_on_movie_end;
bool commandline_stop_emu_on_movie_end;


void commandline_set()
{
	argh::parser cmdl(__argc, __argv,
	                  argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

	commandline_rom = cmdl("--rom", "").str();
	commandline_lua = cmdl("--lua", "").str();
	commandline_st = cmdl("--st", "").str();
	commandline_movie = cmdl("--movie", "").str();
	commandline_avi = cmdl("--avi", "").str();
	commandline_stop_capture_on_movie_end = cmdl["--stop-capture-on-movie-end"];
	commandline_stop_emu_on_movie_end = cmdl["--stop-emu-on-movie-end"];

	// handle "Open With...":
	if (cmdl.size() == 2 && cmdl.params().empty())
	{
		commandline_rom = cmdl[1];
	}
}

void commandline_start_rom()
{
	if (commandline_rom.empty())
	{
		return;
	}

	start_rom(commandline_rom);
}

void commandline_load_st()
{
	if (commandline_st.empty())
	{
		return;
	}

	savestates_do_file(commandline_st.c_str(), e_st_job::load);
}

void commandline_start_lua()
{
	if (commandline_lua.empty())
	{
		return;
	}

	lua_create_and_run(commandline_lua.c_str());
}

void commandline_start_movie()
{
	if (commandline_movie.empty())
	{
		return;
	}

	vcr_start_playback(commandline_movie, nullptr, nullptr);
}

void commandline_start_capture()
{
	if (commandline_avi.empty())
	{
		return;
	}

	vcr_start_capture(commandline_avi.c_str(), false);
}

void commandline_on_movie_playback_stop()
{
	if (commandline_stop_capture_on_movie_end && vcr_is_capturing())
	{
		vcr_stop_capture();
	}

	if (commandline_stop_capture_on_movie_end)
	{
		SendMessage(mainHWND, WM_DESTROY, 0, 0);
	}
}

namespace Cli
{
	void on_task_changed(std::any data)
	{
		auto value = std::any_cast<e_task>(data);
		static auto previous_value = value;


		if (task_is_playback(previous_value) && !task_is_playback(value))
		{
			commandline_on_movie_playback_stop();
		}

		previous_value = value;
	}

	void on_emu_launched_changed(std::any data)
	{
		auto value = std::any_cast<bool>(data);

		if (!value)
			return;

		// start movies, st and lua scripts
		commandline_load_st();
		commandline_start_lua();
		commandline_start_movie();

		// HACK:
		// starting capture immediately won't work, since sample rate will change at game startup, thus terminating the capture
		// as a workaround, we wait a bit before starting the capture
		std::thread([]
		{
			Sleep(1000);
			commandline_start_capture();
		}).detach();
	}

	void on_main_window_created(std::any _)
	{
		commandline_start_rom();
	}

	void init()
	{
		Messenger::subscribe(Messenger::Message::EmuLaunchedChanged, on_emu_launched_changed);
		Messenger::subscribe(Messenger::Message::MainWindowCreated, on_main_window_created);
		commandline_set();
	}
}
