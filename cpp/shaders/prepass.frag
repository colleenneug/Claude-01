#version 410 core
// Depth pre-pass: lay down the depth of everything opaque before shading any
// of it. Two things come out of that. The colour pass then shades each pixel
// once instead of once per surface stacked behind it, which matters more the
// heavier the material shader gets; and the depth buffer exists early enough
// for ambient occlusion and the sun shafts to be worked out from it and
// handed to the colour pass, rather than bolted on afterwards.
void main() {}
