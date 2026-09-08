// This file is part of RockstarEditorPlus.
// Copyright (C) 2026 CoreFX (crxhvrd@proton.me)
// SPDX-License-Identifier: GPL-3.0-only
// RockstarEditorPlus is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License v3 as published by the Free
// Software Foundation. See the LICENSE file for details.

#pragma once

// =============================================================================
//  External camera trajectory (production pipeline extension)
// =============================================================================
//  When enabled, the replay camera is driven from a CSV file of absolute poses
//  sampled over CLIP TIME, and the marker-spline evaluation is bypassed.
//  This is how the capture pipeline maps a move plan (compiled offline against
//  the recorded player path) onto the engine's fixed-time replay: the renderer
//  already steps the replay in exact nanoseconds, so pose-at-t becomes a pure
//  lookup and "same clip, same output" holds for our trajectory exactly as it
//  holds for the engine's own replay state.
//
//  File format (CSV, one sample per line, '#' or ';' start a comment):
//    t_ms,ax,ay,az,bx,by,bz,cx,cy,cz,px,py,pz,fov
//  Rows a/b/c/d follow the game's Matrix34 layout (a = right, b = forward,
//  c = up, d = position), see rdirector. Orientation is interpolated with a
//  proper quaternion slerp (basis -> quat -> slerp -> basis), position and fov
//  linearly. Samples must be sorted ascending in t_ms; t outside the table is
//  clamped to the end poses.
//
//  The file lives next to the ini by default (RockstarEditorPlus\...) and is
//  re-read when its mtime changes (checked about every 2 seconds, the same
//  cadence as Lights.ini), so a trajectory can be swapped while the game is
//  running.
//
//  Keys (RockstarEditorPlus.ini, [RockstarEditorPlus]):
//    ExternalTrajectory=0      ; 1 = drive the camera from the CSV, bypass spline
//    ExternalTrajectoryFile=   ; empty = trajectory.csv in the ini folder
//
//  The frames we drive show up in the trace as
//    STOCK [external trajectory] pos=(..) fwd=(..) fov=..
//  i.e. every driven pose is still logged per frame - the diff against the
//  intended trajectory is the pipeline's acceptance check.
// =============================================================================
namespace extraj
{
	// True when the switch is on AND a usable table is loaded. Called every
	// frame from the spline path; cheap (no I/O - reload is mtime-gated).
	bool active();

	// (Re)load the CSV if the config asks for one and the file changed.
	// Called from the per-frame tick; does its own throttling.
	void tick();

	// Zero the tick throttle so the very next tick stats the file. The
	// pipeline's trigger fires this on consumption: post-render the director
	// parks (no UpdateSmoothing ticks at all), and a table deployed for the
	// next round must be loaded during the bake transition's first ticks -
	// a frozen 120-frame throttle would push the load up to ~120 output
	// frames into the render, contaminating them with the previous round's
	// trajectory (2026-09-08 render_0009).
	void armReload();

	// Drive the director's frame at the current replay time. Caller has
	// already checked active(). Writes the three basis rows, the position
	// and the FOV (clamped to the engine's 1..130).
	void apply(void* director);
}
