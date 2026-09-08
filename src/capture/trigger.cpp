// This file is part of RockstarEditorPlus.
// Copyright (C) 2026 CoreFX (crxhvrd@proton.me)
// SPDX-License-Identifier: GPL-3.0-only
// RockstarEditorPlus is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License v3 as published by the Free
// Software Foundation. See the LICENSE file for details.

#include "main.h"
#include "capture/trigger.h"
#include "capture/exporthook.h"
#include "capture/fxcapture.h"
#include "capture/render.h"
#include "replay/external_trajectory.h"
#include "game/game.h"
#include "game/signatures.h"
#include "utils/config.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <Windows.h>

namespace trigger
{
	namespace
	{
		FILETIME s_mtime{};        // last trigger-file stamp we have consumed
		bool     s_seen    = false; // a stat has succeeded at least once
		bool     s_armed   = false; // a trigger is waiting for an idle renderer
		int      s_cooldown = 0;     // frames left before the next stat

		std::string resolvePath()
		{
			const Config& cfg = Config::get();
			if (!cfg.externalTriggerFile.empty())
			{
				const char* p = cfg.externalTriggerFile.c_str();
				if (p[0] == '\\' || p[0] == '/' || (p[0] && p[1] == ':'))
					return cfg.externalTriggerFile;   // absolute wins
				return paths::file(cfg.externalTriggerFile.c_str());
			}
			return paths::file("trigger.txt");
		}

		FILETIME mtimeOf(const std::string& path)
		{
			FILETIME ft{};
			WIN32_FILE_ATTRIBUTE_DATA fad{};
			if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad))
				ft = fad.ftLastWriteTime;
			return ft;
		}

		bool stampChanged(const FILETIME& a, const FILETIME& b)
		{
			return a.dwLowDateTime != b.dwLowDateTime ||
			       a.dwHighDateTime != b.dwHighDateTime;
		}
	}

	void checkAndFire()
	{
		const Config& cfg = Config::get();

		// Off means off: drop any armed request and forget the stamp so a
		// later re-enable treats the file as new again (no surprise trigger).
		if (!cfg.externalTrigger)
		{
			s_seen  = false;
			s_armed = false;
			s_cooldown = 0;
			return;
		}

		if (s_seen && s_cooldown > 0) { --s_cooldown; }
		if (s_seen && s_cooldown > 0) return;   // throttled, nothing new anyway
		s_cooldown = 10;                           // ~0.2s of frontend frames.
		                                           // Was 120 (~2s): post-render the
		                                           // game throttles to ~1fps, so 120
		                                           // frames took MINUTES and the
		                                           // pipeline's wait expired first
		                                           // (2026-09-08). A stat every 10
		                                           // frames costs nothing measurable.

		const FILETIME now = mtimeOf(resolvePath());

		// No file yet, or unreadable: nothing to fire. Keep any request armed.
		if (now.dwLowDateTime == 0 && now.dwHighDateTime == 0)
		{
			s_seen = true;
			return;
		}

		if (!s_seen)
		{
			// First look at the file: record the stamp, never fire. Otherwise a
			// trigger left over from the last session would render the moment
			// the editor opens. The watchdog always writes a FRESH stamp, and
			// only a stamp that CHANGED since the last look is a request.
			s_seen  = true;
			s_mtime = now;
			return;
		}
		if (stampChanged(s_mtime, now))
		{
			s_mtime = now;
			if (s_armed)
				logger::write("info", "trigger: request superseded by a newer file stamp");
			s_armed = true;
		}

		if (!s_armed) return;

		// Fire only under the same conditions hkOpen applies to a real press.
		// Without the addon/renderer check we would open a REAL bake - the
		// watermark encoder - which the pipeline must never trigger by accident.
		if (!cfg.enableRenderer)
		{
			s_armed = false;
			logger::write("info",
				"trigger: declined - EnableRenderer=0; refusing to open the game's own bake");
			return;
		}
		if (!fxcapture::addonPresent())
		{
			s_armed = false;
			logger::write("info",
				"trigger: declined - capture addon not usable (channel=%s heartbeat=%u)",
				fxcapture::available() ? "mapped" : "NOT MAPPED",
				fxcapture::heartbeat());
			return;
		}
		if (render::active() || exporthook::pending())
		{
			// A render is running (or a diverted bake has not started yet).
			// Keep the request armed; the next idle frontend frame consumes it.
			return;
		}

		s_armed = false;
		logger::write("info", "trigger: external trigger consumed - opening Export/bake");
		// The trajectory table rides along: zero the reload throttle so the
		// next tick stats the file. Consumption happens on the bake
		// transition's first tick - the pipeline has usually JUST deployed
		// the next round's table, and without this the frozen 120-frame
		// throttle would land the load up to ~120 output frames into the
		// render (2026-09-08 render_0009: the first 38 frames followed the
		// previous round's trajectory).
		extraj::armReload();
		game::openPlayback(gsig::PLAYBACK_TYPE_BAKE);
	}
}
