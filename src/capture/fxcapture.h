// This file is part of RockstarEditorPlus.
// Copyright (C) 2026 CoreFX (crxhvrd@proton.me)
// SPDX-License-Identifier: GPL-3.0-only
// RockstarEditorPlus is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License v3 as published by the Free
// Software Foundation. See the LICENSE file for details.

#pragma once
#include <cstdint>
#include <string>

// =============================================================================
//  Frame capture channel
// =============================================================================
//  An ASI cannot read the GPU back buffer; a ReShade addon can. Simple Camera
//  already solved this with a shared-memory channel to a modified
//  IgcsConnector.addon64: the ASI writes an output path and bumps a request
//  counter, the addon grabs the frame on its next present via ReShade's
//  backend-agnostic capture_screenshot(), writes the file and echoes the
//  counter back.
//
//  We speak that EXACT protocol - same mapping name, same struct, same version.
//  So the addon needs no changes and does not care which ASI is driving it.
//  The struct below must stay byte-identical to the copy in the addon's
//  Main.cpp and in Simple Camera's fx_capture.h; if that layout ever changes,
//  all three move together.
//
//  Backend-agnostic matters beyond tidiness: capture_screenshot() works on DX12
//  as well, so this half of video export is already Enhanced-ready.
// =============================================================================

#pragma pack(push, 4)
struct FxCaptureBlock
{
	uint32_t magic;       // 'SCFX' (0x53434658) - set by the ASI once mapped
	uint32_t version;     // 6
	uint32_t requestId;   // ASI increments to request a capture
	uint32_t ackId;       // addon echoes requestId once handled
	uint32_t status;      // 0 = ok, 1 = capture failed, 2 = file write failed
	uint32_t width;       // addon writes the captured dimensions
	uint32_t height;
	uint32_t sampleCount; // motion-blur samples for this output frame (1 = none)
	uint32_t sampleIndex; // which sample, 0..sampleCount-1 (addon accumulates;
	                      // resets on 0, averages + writes on sampleCount-1)
	uint32_t quality;        // JPEG quality 1..100 (ignored for PNG)
	float highlightBoost;    // 0..~1 - extra highlight lift in linear accumulation
	uint32_t addonHeartbeat; // addon bumps this every present; 0 = addon not loaded
	char outPath[512];       // ASI writes the full destination path; the addon
	                         // picks PNG vs JPEG from the .png / .jpg extension
	uint32_t channelOrder;   // 0 = Auto (addon detects the back-buffer format),
	                         // 1 = force RGBA (no swap), 2 = force BGRA (swap R/B)

	// --- autofocus for the depth-of-field session (v7) --------------------
	//
	// Appended, like channelOrder before it, so every earlier offset is
	// unchanged and a mismatched pair still agrees about the capture protocol.
	//
	// The split is forced by what each side can see. Only the ASI can ask the
	// world what is in front of the lens; only the add-on knows maxBokehSize
	// and owns the focus value. So the ASI reports what it MEASURED - a
	// distance and the lens angle that goes with it - and the add-on converts
	// that to its own disparity units. Deliberately not the other way round:
	// maxBokehSize then never crosses the boundary, and dragging it in the
	// panel re-derives focus with no round trip to wait for.
	uint32_t afEnabled;      // addon -> ASI: 1 while the DOF panel wants autofocus
	float    afPointX;       // addon -> ASI: focus point across the frame, 0..1
	float    afPointY;       // addon -> ASI: and down it, 0..1

	uint32_t afResultId;     // ASI -> addon: bumped on every answer written
	uint32_t afStatus;       // ASI -> addon: 0 = ok, 1 = nothing hit,
	                         //               2 = no camera / not in the editor
	float    afDistance;     // ASI -> addon: metres to the hit ALONG THE VIEW
	                         // AXIS - not ray length; the two differ off-centre
	float    afTanHalfHFov;  // ASI -> addon: tan(hfov/2) for the probed frame.
	                         // Sent pre-computed so the vertical-to-horizontal
	                         // conversion happens where the captured aspect is
	                         // already known, and so no angle unit crosses.

	// --- a depth-of-field pass per rendered frame (v8) ---------------------
	//
	// The renderer and a DoF session already do the same thing: accumulate many
	// samples into one image. They differ only in what they vary - the renderer
	// steps TIME, a session sweeps the APERTURE (and time too, through the timed
	// entry point). So this is not a third accumulator; it is the renderer
	// asking the add-on to produce one frame instead of doing it itself.
	//
	// What makes it cheap to wire: a finished session leaves its accumulated
	// image ON SCREEN. So the ordinary capture request that follows grabs it
	// with no new file path, format or buffer anywhere.
	uint32_t dofSeq;         // ASI -> addon: bumped to ask for one DoF pass
	float    dofShutterMs;   // ASI -> addon: the renderer owns the shutter here,
	                         // so the panel's own value is not consulted. One
	                         // shutter, in the place the render is configured.

	uint32_t dofDoneSeq;     // addon -> ASI: echoes dofSeq once the pass has
	                         // finished and the image is on screen
	uint32_t dofStatus;      // addon -> ASI: 0 idle, 1 running, 2 done, 3 failed.
	                         // A pass takes tens of seconds, so "still working"
	                         // has to be distinguishable from "died" - otherwise
	                         // the render's own watchdog is the thing that breaks.

	// --- the lens, pushed by the renderer (v9) -----------------------------
	//
	// Read once when a pass starts, never polled, so the panel stays
	// authoritative at every other moment and there is no instant where both
	// sides own a value. That one-way-at-a-defined-moment rule is the whole
	// reason these can live in two places without drifting.
	//
	// Only what is a per-shot DECISION moves here. The bokeh SHAPE - vertices,
	// rounding, rotation, aberration, fringe - stays in the panel, because you
	// pick it by looking at a live image and a number typed into a text menu
	// cannot be judged.
	float    dofBokehSize;   // aperture diameter, the main creative control
	uint32_t dofQuality;     // ring count; sample total follows from it
	uint32_t dofAutofocus;   // 1 = measure focus in the world each frame
	float    dofFocusX;      // where to measure, across the frame 0..1
	float    dofFocusY;      // and down it

	// --- which camera tool owns the interface (v10) ------------------------
	//
	// The add-on binds to the FIRST loaded module exporting
	// IGCS_StartScreenshotSession, and more than one mod exports it - NVE does.
	// Load order then decides whose camera a depth-of-field session drives, and
	// re-scanning cannot break the tie because the scan is what picks wrong.
	//
	// So the ASI that is driving a render names itself here, and the add-on
	// binds to that module for the duration. Split into two halves because this
	// struct is 4-byte packed and everything else in it is 32 bits.
	uint32_t asiModuleLo;    // ASI -> addon: HMODULE of the tool to bind to,
	uint32_t asiModuleHi;    // 0 = no preference, use whatever was found
};
#pragma pack(pop)

namespace fxcapture
{
	// Map (or attach to) the shared block. Safe to call more than once.
	void init();

	bool available();      // channel mapped
	bool addonPresent();   // addon is alive - it bumps a heartbeat every present

	// Which ReShade, if any, is in this process.
	//
	// The renderer reads the back buffer through a ReShade ADD-ON, so the
	// ordinary ReShade build cannot drive it at all - and that is the single
	// most common reason Export falls back to the game's own encoder. Decided by
	// the same test ReShade's own add-on header uses to find its host: a loaded
	// module exporting ReShadeRegisterAddon.
	//
	// Reported at startup, and read by the export menu so the requirement can be
	// stated on screen with the CURRENT answer next to it rather than as a line
	// in a readme nobody has open.
	enum HostState
	{
		HOST_NONE = 0,   // no ReShade in the process at all
		HOST_NO_ADDONS,  // ReShade, but the build that cannot load add-ons
		HOST_ADDONS,     // ReShade with full add-on support
	};
	HostState hostState();
	bool lastDone();       // addon has acknowledged the most recent request

	// The addon's per-present counter. The render loop uses this as its frame
	// clock: we have no ScriptHookV and therefore no WAIT(0), so "a frame went
	// out" is exactly "this number changed".
	uint32_t heartbeat();

	// Request one motion-blur sub-sample. The addon resets its accumulator on
	// sampleIndex 0, adds each sample, and on the last one averages and writes.
	// sampleCount of 1 is a plain single-frame capture.
	bool requestSample(const char* fullPath, int sampleCount, int sampleIndex);

	void setQuality(int quality);         // JPEG only, 1..100
	void setHighlightBoost(float boost);  // 0..~1
	void setChannelOrder(int order);      // 0 auto / 1 RGBA / 2 BGRA

	// --- autofocus ------------------------------------------------------------
	// The add-on's depth-of-field panel asks for focus; we answer with what is
	// in front of the lens. Kept here rather than exposing the block, which is
	// private to this file on purpose.

	// True while the add-on wants autofocus, with where in the frame it wants
	// focused - 0..1 across and down.
	bool autofocusWanted(float* pointX, float* pointY);

	// The answer. distance is metres along the VIEW AXIS (not ray length) and
	// tanHalfHFov is the horizontal half-angle of the frame it was measured in.
	// status: 0 = ok, 1 = nothing hit, 2 = no camera.
	//
	// Bumps a counter rather than relying on the value changing, so a repeated
	// measurement still reads as a fresh answer - which is what a stationary
	// subject produces.
	void autofocusAnswer(float distance, float tanHalfHFov, uint32_t status);

	// The captured frame's aspect, or 0 if the add-on has not reported a size
	// yet. Needed to turn the game's VERTICAL fov into a horizontal one.
	float capturedAspect();

	// --- a depth-of-field pass per rendered frame ----------------------------
	// Ask the add-on to accumulate one frame across the aperture instead of the
	// renderer accumulating it across time. Returns the request id to wait on.
	uint32_t dofRequest(float shutterMs, float bokehSize, int quality,
	                    bool autofocus, float focusX, float focusY);

	// Has that request finished and left its image on screen?
	bool dofDone(uint32_t seq);

	// 0 idle, 1 running, 2 done, 3 failed. Separates "a 50-second frame is still
	// going" from "the pass died", which a timeout alone cannot.
	uint32_t dofStatus();

	// Tear any pass down. Idempotent - the end of a render, an abort and a
	// cancel all come through here.
	void dofEnd();

	// Create a fresh auto-numbered output folder for a sequence render.
	// `base` may be null or empty, in which case a single
	// RockstarEditorPlus_Captures folder beside GTA5.exe is used. Either way the
	// render itself gets a numbered subfolder inside it.
	bool newSequenceFolder(const char* base, char* outFolder, int cap);

	// Where renders WILL go, without creating anything. Same resolution
	// newSequenceFolder uses, minus the numbered subfolder - so it can be logged
	// at startup, before any render exists. Under FiveM this is beside the .asi
	// in plugins\, which is not where people look first.
	std::string captureBaseDir(const char* base);
}
