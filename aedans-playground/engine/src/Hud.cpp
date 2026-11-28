#include "playground/engine/Hud.h"

#include <algorithm>
#include <format>
#include <string>

#include <imgui.h>

namespace playground::engine::hud {
namespace {

// The large HUD font (DebugUi adds it second), or the default.
ImFont* hudFont() {
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  return atlas->Fonts.Size > 1 ? atlas->Fonts[1] : ImGui::GetFont();
}

ImU32 rgba(f32 r, f32 g, f32 b, f32 a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a)); }

}  // namespace

void letterbox(f32 amount) {
  amount = std::clamp(amount, 0.0f, 1.0f);
  if (amount <= 0.0f) return;
  const ImGuiViewport* view = ImGui::GetMainViewport();
  const f32 bar = view->Size.y * 0.12f * amount;
  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  const ImU32 black = rgba(0.0f, 0.0f, 0.0f, 1.0f);
  draw->AddRectFilled(view->Pos, ImVec2(view->Pos.x + view->Size.x, view->Pos.y + bar), black);
  draw->AddRectFilled(ImVec2(view->Pos.x, view->Pos.y + view->Size.y - bar),
                      ImVec2(view->Pos.x + view->Size.x, view->Pos.y + view->Size.y), black);
}

void subtitle(std::string_view speaker, std::string_view text, f32 opacity) {
  if (text.empty()) return;
  const ImGuiViewport* view = ImGui::GetMainViewport();
  ImFont* font = hudFont();
  const f32 size = font->FontSize;
  const std::string line = speaker.empty() ? std::string(text) : std::format("{}: {}", speaker, text);
  const ImVec2 extent = font->CalcTextSizeA(size, view->Size.x * 0.9f, view->Size.x * 0.9f, line.c_str());
  const ImVec2 at(view->Pos.x + (view->Size.x - extent.x) * 0.5f, view->Pos.y + view->Size.y * 0.81f - extent.y);
  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  // A soft dark band and a drop shadow: readable over any scene.
  draw->AddRectFilled(ImVec2(at.x - 16.0f, at.y - 8.0f), ImVec2(at.x + extent.x + 16.0f, at.y + extent.y + 8.0f),
                      rgba(0.0f, 0.0f, 0.0f, 0.55f * opacity), 6.0f);
  draw->AddText(font, size, ImVec2(at.x + 2.0f, at.y + 2.0f), rgba(0.0f, 0.0f, 0.0f, opacity), line.c_str(), nullptr,
                view->Size.x * 0.9f);
  draw->AddText(font, size, at, rgba(1.0f, 0.97f, 0.88f, opacity), line.c_str(), nullptr, view->Size.x * 0.9f);
}

void skipPrompt(std::string_view keys, f32 progress, std::string_view note) {
  const ImGuiViewport* view = ImGui::GetMainViewport();
  ImFont* font = ImGui::GetFont();
  const f32 size = font->FontSize * 1.3f;
  const std::string label = std::format("Hold {} to skip{}{}", keys, note.empty() ? "" : "  ", note);
  const ImVec2 extent = font->CalcTextSizeA(size, 1e6f, 0.0f, label.c_str());
  const ImVec2 at(view->Pos.x + view->Size.x - extent.x - 28.0f, view->Pos.y + view->Size.y - extent.y - 30.0f);
  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  draw->AddText(font, size, at, rgba(1.0f, 1.0f, 1.0f, 0.85f), label.c_str());
  const ImVec2 barFrom(at.x, at.y + extent.y + 5.0f);
  const ImVec2 barTo(at.x + extent.x, barFrom.y + 4.0f);
  draw->AddRectFilled(barFrom, barTo, rgba(1.0f, 1.0f, 1.0f, 0.2f), 2.0f);
  draw->AddRectFilled(barFrom, ImVec2(barFrom.x + extent.x * std::clamp(progress, 0.0f, 1.0f), barTo.y),
                      rgba(1.0f, 0.85f, 0.35f, 0.95f), 2.0f);
}

}  // namespace playground::engine::hud
