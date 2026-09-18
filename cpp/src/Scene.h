#pragma once
#include "Gl.h"
#include "Draw.h"
#include <vector>

// Everything Renderer needs from "the world", so it can draw more than one
// kind of one. A mission (Game) and open space (Space) are genuinely
// different worlds — different geometry, different sun, different sense of
// scale — but the shadow/HDR/bloom/fog pipeline in between is identical,
// and duplicating it per mode would be the actual mistake.
//
// Virtual dispatch here costs a handful of calls per frame against a pass
// that already issues hundreds of draws; it is not worth a template.
class SceneSource {
public:
  virtual ~SceneSource() = default;

  // Fill `out` with everything to draw this frame.
  virtual void collect(float time, std::vector<DrawItem>& out) const = 0;

  // The one directional light the whole pipeline is built around: the
  // direction light travels along, its colour, and its intensity.
  virtual glm::vec3 sunDirection() const = 0;
  virtual glm::vec3 sunColour() const = 0;
  virtual float sunIntensity() const = 0;

  // Camera-relative particles (dust in a mission, stars in space). A scene
  // with none returns an empty VAO and a count of zero.
  virtual GLuint moteVao() const = 0;
  virtual float moteBoxSize() const = 0;
  virtual int moteCount() const = 0;
  virtual glm::vec3 moteColour() const { return glm::vec3(0.75f, 0.85f, 0.95f); }
  virtual float moteSize() const { return 26.0f; }
  virtual float moteOpacity() const { return 0.22f; }
  // Dust gets bigger as it drifts toward the lens; a star does not. With
  // distance scaling on, point size is size/distance — right for motes a
  // few metres away, and wrong for a starfield spread over twenty thousand
  // units, where it collapses every star to one clamped pixel.
  virtual bool moteDistanceScaled() const { return true; }

  // How strongly the image-based lighting probe contributes. The probe is
  // a capture of a lit interior (see IBL::build), which is right for a
  // mission and wrong in deep space: reflected off a planet it reads as
  // giant coloured arcs sitting in the same place on screen no matter
  // which world you're looking at. Out there the sun is the only light.
  virtual float iblIntensity() const { return 1.0f; }

  // A structureless fill added on top of the probe. Turning the probe
  // down is not the same as turning the light down: at a tenth strength
  // its cube faces still read as hard-edged panels on the night side of
  // something as big on screen as a planet. A scene that wants a little
  // fill and none of the probe's shape asks for it here.
  virtual glm::vec3 ambientFill() const { return glm::vec3(0.0f); }

  // What the HDR buffer is cleared to before anything is drawn. A mission's
  // is a dim dust haze; space wants near-black so the stars read at all.
  virtual glm::vec3 clearColour() const { return glm::vec3(0.02f, 0.018f, 0.03f); }

  // How far the far plane and the shadow cascades have to reach. A mission
  // arena is tens of metres; open space is tens of thousands, and clamping
  // both to one number would either clip the planets away or throw all the
  // shadow resolution at empty air.
  virtual float viewDistance() const { return 500.0f; }

  // Whether cascaded shadows are worth rendering at all. Open space has a
  // handful of convex bodies thousands of units apart casting onto nothing.
  virtual bool wantsShadows() const { return true; }

  // Atmosphere. The defaults are the dusty-planet haze the mission arenas
  // are graded around; open space overrides them to nothing. This has to be
  // per-scene rather than a renderer constant, because the fog is an
  // analytic integral along the view ray: at mission distances it's a
  // gentle depth cue, and at the tens of thousands of units space works in
  // the same numbers saturate and flood the frame with pink haze.
  virtual float fogDensity() const { return 0.018f; }
  virtual float fogFalloff() const { return 0.05f; }
  virtual float fogBase() const { return -1.0f; }
  virtual glm::vec3 fogColour() const { return glm::vec3(0.42f, 0.30f, 0.34f); }
  virtual float fogInscatter() const { return 0.9f; }
};
