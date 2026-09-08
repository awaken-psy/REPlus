// This file is part of RockstarEditorPlus.
// Copyright (C) 2026 CoreFX (crxhvrd@proton.me)
// SPDX-License-Identifier: GPL-3.0-only
// RockstarEditorPlus is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License v3 as published by the Free
// Software Foundation. See the LICENSE file for details.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "main.h"
#include "utils/log.h"
#include "game/game.h"
#include "replay/external_trajectory.h"
#include "replay/marker.h"     // rdirector frame offsets
#include "replay/quat.h"
#include "replay/spline.h"
#include "utils/config.h"

namespace extraj
{
	namespace
	{
		struct Sample
		{
			float t = 0;                 // clip time, milliseconds
			spline::Vec3 a, b, c, d;     // Matrix34 rows: right, forward, up, position
			float fov = 59.0f;
		};

		std::vector<Sample> s_samples;
		std::string s_path;          // resolved absolute path of the CSV
		FILETIME s_mtime{};         // last mtime we loaded with
		bool s_loadedOnce = false;  // a load attempt has happened (success or not)
		int s_cooldown = 0;         // frames left before the next mtime check

		std::string resolvePath()
		{
			const Config& cfg = Config::get();
			if (!cfg.iniPath.empty())
			{
				const std::string dir = cfg.iniPath.substr(0,
					cfg.iniPath.find_last_of("\\/") + 1);
				if (!cfg.externalTrajectoryFile.empty())
				{
					// Absolute paths win; anything else is relative to the
					// ini's own folder, same folder the sidecar stores use.
					const char* p = cfg.externalTrajectoryFile.c_str();
					if (p[0] == '\\' || p[0] == '/' || (p[0] && p[1] == ':'))
						return cfg.externalTrajectoryFile;
					return dir + cfg.externalTrajectoryFile;
				}
				return dir + "trajectory.csv";
			}
			return "trajectory.csv";
		}

		FILETIME mtimeOf(const std::string& path)
		{
			FILETIME ft{};
			WIN32_FILE_ATTRIBUTE_DATA fad{};
			if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad))
				ft = fad.ftLastWriteTime;
			return ft;
		}

		bool parseFloats(const char* line, float* out, int count)
		{
			char* end = nullptr;
			const char* p = line;
			for (int i = 0; i < count; ++i)
			{
				while (*p == ' ' || *p == '\t' || *p == ',') ++p;
				if (!*p) return false;
				out[i] = strtof(p, &end);
				if (end == p) return false;   // not a number at all
				p = end;
			}
			return true;
		}

		// Write one held pose into the director's frame. Clamps the FOV to the
		// engine's own 1..130 so a table typo cannot ask the game for a
		// fisheye-3-degree shot it would refuse to render anyway.
		void writePose(void* director, const spline::Vec3& a,
			const spline::Vec3& b, const spline::Vec3& c,
			const spline::Vec3& d, float fov)
		{
			auto* base = (uint8_t*)director;
			a.store((float*)(base + rdirector::OFF_FrameMatrixA));
			b.store((float*)(base + rdirector::OFF_FrameMatrixB));
			c.store((float*)(base + rdirector::OFF_FrameMatrixC));
			d.store(rdirector::framePosition(director));
			if (fov < 1.0f) fov = 1.0f;
			if (fov > 130.0f) fov = 130.0f;
			*(float*)(base + rdirector::OFF_FrameFov) = fov;
		}

		void load()
		{
			s_samples.clear();

			FILE* f = fopen(s_path.c_str(), "r");
			if (!f)
			{
				logger::write("info",
					"extraj: no trajectory at '%s' (checked because the key is on)",
					s_path.c_str());
				return;
			}

			char line[1024];
			int lineno = 0, loaded = 0;
			bool unordered = false;
			float prevT = -1.0f;
			while (fgets(line, sizeof(line), f))
			{
				++lineno;
				const char* p = line;
				while (*p == ' ' || *p == '\t') ++p;
				if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || !*p)
					continue;

				float v[14];
				if (!parseFloats(p, v, 14))
				{
					// Column count is fixed; a malformed line is reported and
					// skipped rather than failing the whole file - one bad row
					// in a multi-thousand-sample export should not kill the shot.
					logger::write("info",
						"extraj: %s:%d - expected 14 columns, line skipped",
						s_path.c_str(), lineno);
					continue;
				}

				Sample s;
				s.t   = v[0];
				s.a   = spline::Vec3(v[1],  v[2],  v[3]);
				s.b   = spline::Vec3(v[4],  v[5],  v[6]);
				s.c   = spline::Vec3(v[7],  v[8],  v[9]);
				s.d   = spline::Vec3(v[10], v[11], v[12]);
				s.fov = v[13];
				if (s.t < prevT) unordered = true;
				prevT = s.t;
				s_samples.push_back(s);
				++loaded;
			}
			fclose(f);

			// The lookup below is a straight scan assuming ascending t; an
			// unordered file is a compile error on the pipeline side and must be
			// refused loudly, not quietly mis-driven.
			if (unordered)
			{
				logger::write("info",
					"extraj: %s - samples not ascending in t_ms, table refused",
					s_path.c_str());
				s_samples.clear();
				return;
			}

			logger::write("info", "extraj: loaded %d samples from '%s'",
				loaded, s_path.c_str());
		}
	}

	void tick()
	{
		const Config& cfg = Config::get();
		if (!cfg.externalTrajectory) { s_loadedOnce = false; s_samples.clear(); return; }

		// Throttle the stat call to roughly one every two seconds of editor
		// frames, the same cadence Lights.ini uses for its hot reload.
		if (s_loadedOnce && s_cooldown > 0) { --s_cooldown; return; }
		s_cooldown = 120;

		const std::string want = resolvePath();
		const FILETIME ft = mtimeOf(want);

		const bool pathChanged = want != s_path;
		const bool stampChanged = ft.dwLowDateTime != s_mtime.dwLowDateTime ||
		                          ft.dwHighDateTime != s_mtime.dwHighDateTime;

		if (!s_loadedOnce || pathChanged || stampChanged)
		{
			s_path = want;
			s_mtime = ft;
			s_loadedOnce = true;
			load();
		}
	}

	void armReload()
	{
		// The throttle counts editor frames, and across a parked director it
		// freezes at whatever remained - the next burst of ticks burns it off
		// before the stat finally happens. Zeroing it here (trigger
		// consumption) makes the very next tick stat the file, which is the
		// whole point: the bake transition's first ticks must carry the NEW
		// table, not the previous round's.
		s_cooldown = 0;
	}

	bool active()
	{
		return Config::get().externalTrajectory && !s_samples.empty();
	}

	void apply(void* director)
	{
		if (s_samples.empty()) return;
		if (!game::addr_g_ReplayTimeMs) return;

		const float now = *(float*)game::addr_g_ReplayTimeMs;

		// End clamps. A table that starts later than the clip or ends earlier
		// still yields a well-defined pose - the held end keyframe - rather
		// than a half-written frame.
		if (now <= s_samples.front().t)
		{
			const Sample& s = s_samples.front();
			writePose(director, s.a, s.b, s.c, s.d, s.fov);
			return;
		}
		if (now >= s_samples.back().t)
		{
			const Sample& s = s_samples.back();
			writePose(director, s.a, s.b, s.c, s.d, s.fov);
			return;
		}

		// Linear scan from the front is O(1) amortised for a frame-ordered
		// caller (each frame advances at most one or two samples); a render
		// holds the playhead and lands on the same sample repeatedly. Binary
		// search is not worth the branchy code for at most a few thousand rows.
		size_t i = 0;
		while (i + 2 < s_samples.size() && s_samples[i + 1].t <= now) ++i;

		const Sample& s0 = s_samples[i];
		const Sample& s1 = s_samples[i + 1];
		const float span = s1.t - s0.t;
		float u = (span > 1e-6f) ? (now - s0.t) / span : 0.0f;
		if (u < 0.0f) u = 0.0f;
		if (u > 1.0f) u = 1.0f;

		// Position and fov interpolate linearly; orientation takes the
		// sphere-short way via quaternions so a 170-degree cut in the plan
		// cannot take the long way round through the opposite hemisphere.
		const spline::Vec3 pos = s0.d + (s1.d - s0.d) * u;
		const float fov = s0.fov + (s1.fov - s0.fov) * u;

		const rquat::Quat q0 = rquat::fromBasis(s0.a, s0.b, s0.c);
		const rquat::Quat q1 = rquat::fromBasis(s1.a, s1.b, s1.c);
		const rquat::Quat q  = rquat::slerp(q0, q1, u);

		float a[3], b[3], c[3];
		rquat::toMatrixRows(q, a, b, c);
		writePose(director,
			spline::Vec3(a[0], a[1], a[2]),
			spline::Vec3(b[0], b[1], b[2]),
			spline::Vec3(c[0], c[1], c[2]), pos, fov);
	}
}
