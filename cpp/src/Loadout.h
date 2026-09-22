#pragma once
#include "Gl.h"
#include "Content.h"
#include "Profile.h"
#include <string>
#include <vector>

// THE KIT SCREEN — your gear, from anywhere, on one key.
//
// The Hub already lets you change gear, but only from a counter in a hub:
// you have to be standing in front of Ondo or Voss to swap a rifle. That is
// fine for shopping and useless for the thing you actually want, which is
// realising three rooms into a building that you brought the wrong weapon.
//
// So this is a modal overlay rather than an app state. It can be opened over
// a mission, a station, open space or the hub itself, it owns input while it
// is up, and it writes straight to the Profile. Whoever is underneath is
// told to re-read the profile when it closes (see Game::applyLoadout).
//
// IT DOES NOT PAUSE A MISSION. The world keeps running underneath and you are
// still shootable, exactly like a cutscene. That is deliberate: a menu that
// freezes a firefight while you shop is a menu that removes the decision it
// exists to serve. Opening your kit with a warden ten metres away should be a
// bad idea.
class Loadout {
public:
  // Four columns, in the order you think about them.
  enum class Column : int { Primary = 0, Sidearm, Armour, Shader, Count };

  void open(const Content& content, Profile& profile);
  void close() { open_ = false; }
  [[nodiscard]] bool isOpen() const { return open_; }

  // Advances one frame while open. Returns true on the frame it closes, so
  // the caller knows to re-read the profile exactly once. `scripted`, when
  // non-null, substitutes for the keyboard — one of "up", "down", "left",
  // "right", "equip", "close" — and is used only by headless verification.
  bool update(GLFWwindow* window, const char* scripted = nullptr);

  // ---- what the renderer needs
  [[nodiscard]] Column column() const { return column_; }
  [[nodiscard]] int row(Column c) const { return rows_[(int)c]; }
  // The ids listed under each column, in display order.
  [[nodiscard]] const std::vector<std::string>& ids(Column c) const { return ids_[(int)c]; }
  // One line at the bottom: what the last Enter actually did, or why it did
  // nothing. A menu that silently refuses is a menu you think is broken.
  [[nodiscard]] const std::string& notice() const { return notice_; }
  [[nodiscard]] float noticeAge() const { return noticeT_; }

  // Whether the item under the caret can be taken, and if not, why. Shared by
  // the renderer and by the equip path so the two can never disagree about
  // what is available.
  enum class Availability { Equipped, Owned, Affordable, TooPoor, RankLocked, WrongDoctrine, Empty };
  [[nodiscard]] Availability availability(Column c, const std::string& id) const;
  [[nodiscard]] std::string gateLabel(Column c, const std::string& id) const;

private:
  void rebuild();
  bool equipSelected();
  void setNotice(const std::string& s) { notice_ = s; noticeT_ = 0.0f; }

  const Content* content_ = nullptr;
  Profile* profile_ = nullptr;
  bool open_ = false;

  Column column_ = Column::Primary;
  int rows_[(int)Column::Count] = {0, 0, 0, 0};
  std::vector<std::string> ids_[(int)Column::Count];

  std::string notice_;
  float noticeT_ = 99.0f;

  bool prevUp_ = false, prevDown_ = false, prevLeft_ = false, prevRight_ = false;
  bool prevEquip_ = false, prevClose_ = false;
};
