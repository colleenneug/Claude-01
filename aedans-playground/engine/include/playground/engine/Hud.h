#pragma once
// =============================================================================
//  playground/engine/Hud.h — on-screen text and shapes for the game:
//  cinematic letterbox bars, subtitles, a hold-to-skip prompt.
//
//  Drawn with Dear ImGui's draw lists, so only from a DebugUi HUD callback
//  (DebugUi::setHud): that is when there is an ImGui frame. They go behind
//  any developer panels and over the 3D scene.
// =============================================================================

#include "playground/core/Types.h"

#include <string_view>

namespace playground::engine::hud {

// Black bars at the top and bottom: `amount` 0 (none) .. 1 (12% of the height each).
void letterbox(f32 amount);
// A line of dialogue near the bottom, centred, with the speaker's name.
void subtitle(std::string_view speaker, std::string_view text, f32 opacity = 1.0f);
// "Hold <keys> to skip" in a corner, with a bar filling as the key is held.
void skipPrompt(std::string_view keys, f32 progress, std::string_view note = {});

}  // namespace playground::engine::hud
