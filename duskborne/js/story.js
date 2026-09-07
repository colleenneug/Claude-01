/* ============================================================
   Lore and briefing copy. All original: the Hollow, the Unseen, the
   Lumen, the Sunken Throne and the Coilqueen are this game's own
   setting, not a retelling of anyone else's.
   ============================================================ */
(function (DB) {
  'use strict';

  const INTRO = [
    'Before the Lumen had a name for the light they carry, something older '
    + 'had already noticed the dark between the stars was listening back.',
    'They call it the Hollow. It does not grant power so much as recognise '
    + 'it was always yours, and stop pretending otherwise.',
    'You were recognised six days ago. The Unseen has not stopped watching '
    + 'since.'
  ];

  const TUTORIAL_BRIEFING = {
    title: 'FIRST COMMUNION',
    lines: [
      'The Unseen speaks first in drills, not sermons. Move. Shoot. Reach '
      + 'for what it gave you.',
      'This chamber is empty of anything that can hurt you. Use that. '
      + 'Whatever comes after will not be so generous.'
    ]
  };

  const THRONE_BRIEFING = {
    title: 'THE SUNKEN THRONE',
    lines: [
      'The Coilqueen’s realm folds space the way a held breath folds a '
      + 'lung — wrong, and only for as long as she allows it.',
      'The Unseen does not care what she is building down there. It cares '
      + 'that Lumen Wardens have found the entrance first, Light drawn and '
      + 'burning, and that they are between you and it.',
      'Clear them. Whatever leads them will not run when the rest do.'
    ]
  };

  const DEBRIEF_WIN = [
    'The Hollow does not applaud. It simply stops resisting you quite so '
    + 'much, which is the only compliment it knows how to give.'
  ];

  const DEBRIEF_LOSE = [
    'The dark caught you before it killed you. That is not mercy — the '
    + 'Unseen was not finished using you yet.'
  ];

  DB.story = { INTRO: INTRO, TUTORIAL_BRIEFING: TUTORIAL_BRIEFING,
               THRONE_BRIEFING: THRONE_BRIEFING,
               DEBRIEF_WIN: DEBRIEF_WIN, DEBRIEF_LOSE: DEBRIEF_LOSE };
})(window.DB || (window.DB = {}));
