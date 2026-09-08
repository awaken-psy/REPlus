// This file is part of RockstarEditorPlus.
// Copyright (C) 2026 CoreFX (crxhvrd@proton.me)
// SPDX-License-Identifier: GPL-3.0-only
// RockstarEditorPlus is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License v3 as published by the Free
// Software Foundation. See the LICENSE file for details.

#pragma once

// =============================================================================
//  External render trigger (pipeline extension)
// =============================================================================
//  A trigger FILE turns "press Export" into a command-line action. The
//  watchdog/orchestrator rewrites the file (any content - the mtime is the
//  signal, the same trick the trajectory table uses for hot reload), and the
//  next frontend frame opens the Export/bake exactly as the button would.
//
//  Keys (RockstarEditorPlus.ini, [RockstarEditorPlus]):
//    ExternalTrigger=0       ; 1 = watch the trigger file
//    ExternalTriggerFile=     ; empty = trigger.txt in the mod folder
//
//  Firing conditions mirror the ones hkOpen applies to a real button press
//  (renderer on, capture addon usable) so a trigger can never fall through to
//  the game's own watermark bake. A trigger that arrives mid-render stays
//  armed and is consumed when the renderer goes idle again - exactly one
//  pending request, never a queue.
// =============================================================================
namespace trigger
{
	// Call once per frontend frame (hkPointer - the same context the Export
	// button's TriggerExport runs in). Cheap: the file stat is throttled to
	// about once every two seconds, the same cadence as the trajectory reload.
	void checkAndFire();
}
