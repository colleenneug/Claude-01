#pragma once
#include "Gl.h"
#include "Draw.h"
#include "Content.h"
#include <string>
#include <vector>

// The people in the Cradle.
//
// A hub is not a room, it is the people standing in it. Five of them have
// posts and a job you can walk up to and do; the rest are crew, walking
// their own routes across the three decks or leaning on a gallery rail. The
// walkers are not decoration for its own sake: people you have to walk
// around are what make a room feel used rather than like a lobby.
//
// The figures are deliberately *not* the enemy rig (Hostile::collectWalker).
// Those are armoured frames — plated, jointed, visored. These are people in
// coats, built lighter and rounder, because you should never have to look
// twice to tell a quartermaster from something that came off the ark.
class Crew {
public:
  // How someone is standing when they are not going anywhere. A room where
  // everybody stands the same way reads as a shop window, however good the
  // individual figure is.
  enum class Posture : int { Neutral = 0, ArmsCrossed, HandsBehindBack, HandOnHip };

  struct Person {
    const CrewDef* def = nullptr;   // null for the unnamed crew walking routes
    glm::vec3 pos{0.0f};
    float yaw = 0.0f;
    glm::vec3 tint{0.3f, 0.33f, 0.38f};
    glm::vec3 accent{0.6f, 0.7f, 0.85f};

    // What makes one person not another. All of it is deterministic from the
    // person's index — a crew that is differently shaped every time you dock
    // is a crew you cannot recognise, and recognising the quartermaster from
    // across the concourse is the whole reason she is standing there.
    float height = 1.78f;        // 1.58 .. 1.92 metres
    float build = 1.0f;          // 0.88 .. 1.14; shoulder and limb thickness
    glm::vec3 skin{0.62f, 0.46f, 0.36f};
    glm::vec3 hair{0.16f, 0.12f, 0.10f};
    glm::vec3 trousers{0.22f, 0.23f, 0.26f};
    int hairStyle = 0;           // 0 short, 1 cropped, 2 tied back, 3 under a cap
    Posture posture = Posture::Neutral;

    // A walker follows `route` at a stroll; everyone else stands where they
    // were put. `phase` keeps two people side by side from moving in lockstep.
    std::vector<glm::vec3> route;
    size_t leg = 0;
    float speed = 1.6f;
    float phase = 0.0f;
  };

  // Loads every content/crew/*.cfg plus the wandering crew and the idlers.
  void init(const Content& content);
  void update(float dt);

  void collect(std::vector<DrawItem>& out) const;

  // The named crew member within `range` of `at` and on the same deck, or
  // null. Only the ones with posts can be talked to — the walkers are going
  // somewhere.
  const Person* nearestPost(const glm::vec3& at, float range) const;

  const std::vector<Person>& people() const { return people_; }

private:
  std::vector<Person> people_;
  float time_ = 0.0f;
};
