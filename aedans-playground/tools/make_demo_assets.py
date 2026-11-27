#!/usr/bin/env python3
"""Writes the demo's 3D models (app/assets/models/) as glTF 2.0.

Everything is generated here, from code, so the models are ours (no licence
to track) and can be remade at any time:

    python3 tools/make_demo_assets.py

Only the Python standard library is used (zlib for PNG, struct for binary).

  crate.gltf    A wooden crate with a metal frame: a cube with three 256x256
                textures (base colour, normal map, metallic-roughness), a
                separate .bin buffer and PNG files. No tangents, on purpose:
                the engine has to make them.
  spheres.glb   Seven spheres in one binary .glb, one material each: from
                rough plastic to polished gold. One mesh, seven nodes.
  floor.gltf    A 24 x 24 floor whose checker texture repeats 12 times
                (sampler REPEAT, mipmaps and anisotropic filtering show here).
  lamp.glb      A dark post with a glowing bulb: emissiveFactor x
                KHR_materials_emissive_strength (12x), bright enough to bloom.
"""

import json
import math
import pathlib
import struct
import zlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "app" / "assets" / "models"

# --- PNG ------------------------------------------------------------------------


def write_png(path, width, height, pixel):
    """pixel(x, y) -> (r, g, b) bytes; RGB, 8 bits, top row first."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter: none
        for x in range(width):
            rows.extend(pixel(x, y))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def clamp_byte(v):
    return max(0, min(255, int(round(v))))


def normal_map(width, height, height_at, strength):
    """A tangent-space normal map from a height field.

    x right along u, y UP the image (glTF: green = up), z out of the surface.
    The image's rows run down, so "up" is towards row - 1.
    """
    def pixel(x, y):
        dx = (height_at((x + 1) % width, y) - height_at((x - 1) % width, y)) * 0.5
        dy_up = (height_at(x, (y - 1) % height) - height_at(x, (y + 1) % height)) * 0.5
        nx, ny, nz = -dx * strength, -dy_up * strength, 1.0
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        return bytes(clamp_byte((c / length * 0.5 + 0.5) * 255) for c in (nx, ny, nz))
    return pixel


# --- glTF building ------------------------------------------------------------------


class Gltf:
    """Collects buffers, views and accessors for one model."""

    def __init__(self):
        self.binary = bytearray()
        self.doc = {"asset": {"version": "2.0", "generator": "aedans-playground tools/make_demo_assets.py"},
                    "bufferViews": [], "accessors": [], "meshes": [], "nodes": [], "materials": [],
                    "scenes": [{"nodes": []}], "scene": 0}

    def _view(self, data, target):
        while len(self.binary) % 4:
            self.binary.append(0)
        offset = len(self.binary)
        self.binary.extend(data)
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        return len(self.doc["bufferViews"]) - 1

    def floats(self, rows, kind, with_bounds=False):
        """rows: list of tuples of floats; kind: "VEC2", "VEC3", "VEC4"."""
        data = b"".join(struct.pack("<%df" % len(r), *r) for r in rows)
        accessor = {"bufferView": self._view(data, 34962), "componentType": 5126, "count": len(rows), "type": kind}
        if with_bounds:  # glTF requires min/max for POSITION
            accessor["min"] = [min(r[i] for r in rows) for i in range(len(rows[0]))]
            accessor["max"] = [max(r[i] for r in rows) for i in range(len(rows[0]))]
        self.doc["accessors"].append(accessor)
        return len(self.doc["accessors"]) - 1

    def indices(self, values):
        data = struct.pack("<%dH" % len(values), *values)
        self.doc["accessors"].append({"bufferView": self._view(data, 34963), "componentType": 5123,
                                      "count": len(values), "type": "SCALAR"})
        return len(self.doc["accessors"]) - 1

    def mesh(self, name, positions, normals, uvs, indices, material):
        attributes = {"POSITION": self.floats(positions, "VEC3", with_bounds=True),
                      "NORMAL": self.floats(normals, "VEC3"), "TEXCOORD_0": self.floats(uvs, "VEC2")}
        self.doc["meshes"].append({"name": name, "primitives": [
            {"attributes": attributes, "indices": self.indices(indices), "material": material}]})
        return len(self.doc["meshes"]) - 1

    def node(self, name, mesh, translation=None, in_scene=True):
        node = {"name": name, "mesh": mesh}
        if translation:
            node["translation"] = translation
        self.doc["nodes"].append(node)
        if in_scene:
            self.doc["scenes"][0]["nodes"].append(len(self.doc["nodes"]) - 1)

    def write_gltf(self, path, bin_name):
        self.doc["buffers"] = [{"byteLength": len(self.binary), "uri": bin_name}]
        (path.parent / bin_name).write_bytes(bytes(self.binary))
        path.write_text(json.dumps(self.doc, indent=1))

    def write_glb(self, path):
        while len(self.binary) % 4:
            self.binary.append(0)
        self.doc["buffers"] = [{"byteLength": len(self.binary)}]
        text = json.dumps(self.doc, separators=(",", ":")).encode()
        text += b" " * (-len(text) % 4)
        total = 12 + 8 + len(text) + 8 + len(self.binary)
        path.write_bytes(struct.pack("<4sII", b"glTF", 2, total) +
                         struct.pack("<I4s", len(text), b"JSON") + text +
                         struct.pack("<I4s", len(self.binary), b"BIN\0") + bytes(self.binary))


def check_winding(positions, normals, indices):
    """Every triangle must be counter-clockwise seen from where its normals point."""
    for t in range(0, len(indices), 3):
        a, b, c = (positions[i] for i in indices[t:t + 3])
        e1 = [b[k] - a[k] for k in range(3)]
        e2 = [c[k] - a[k] for k in range(3)]
        face = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        n = normals[indices[t]]
        if sum(face[k] * n[k] for k in range(3)) < -1e-9:
            raise AssertionError("triangle %d is wound clockwise" % (t // 3))


# --- The crate --------------------------------------------------------------------------


def crate():
    size = 256
    border = 26  # the metal frame, in pixels

    def is_frame(x, y):
        return x < border or y < border or x >= size - border or y >= size - border

    def plank_height(x, y):
        """Frame raised, planks flat with grooves between them, rivets on the frame."""
        if is_frame(x, y):
            # Rivets: small domes in the frame's corners and middles.
            h = 1.0
            for cx in (border // 2, size // 2, size - border // 2):
                for cy in (border // 2, size // 2, size - border // 2):
                    d = math.hypot(x - cx, y - cy)
                    if d < 6 and (is_frame(cx, cy)):
                        h += 0.6 * math.cos(d / 6 * math.pi / 2)
            return h
        plank = (y - border) * 4 // (size - 2 * border)  # four horizontal planks
        top = border + plank * (size - 2 * border) // 4
        local = y - top
        return 0.0 if local < 3 else 0.3

    def wood(x, y):
        grain = math.sin(x * 0.11 + 3.0 * math.sin(y * 0.045) + y * 0.01) * 0.5 + 0.5
        plank = (y - border) * 4 // (size - 2 * border)
        shade = (0.85, 1.0, 0.92, 1.06)[plank % 4]
        return [c * (0.75 + 0.25 * grain) * shade for c in (150, 98, 52)]

    def base_colour(x, y):
        if is_frame(x, y):
            v = 120 + 14 * math.sin(x * 0.7 + y * 0.3)
            return bytes(clamp_byte(c) for c in (v * 1.05, v, v * 0.95))
        if plank_height(x, y) == 0.0:
            return bytes(clamp_byte(c * 0.4) for c in wood(x, y))  # dark grooves
        return bytes(clamp_byte(c) for c in wood(x, y))

    def metallic_roughness(x, y):
        # glTF: green = roughness, blue = metallic.
        if is_frame(x, y):
            return bytes((0, 95, 255))
        return bytes((0, 210, 0))

    write_png(OUT / "textures" / "crate_basecolor.png", size, size, base_colour)
    write_png(OUT / "textures" / "crate_normal.png", size, size, normal_map(size, size, plank_height, 3.0))
    write_png(OUT / "textures" / "crate_metallic_roughness.png", size, size, metallic_roughness)

    # A unit cube, 24 vertices: each face its own four corners, normals and uvs.
    faces = [  # (normal, u axis, v axis): v runs DOWN the texture, so -up on the face
        ((1, 0, 0), (0, 0, -1), (0, -1, 0)), ((-1, 0, 0), (0, 0, 1), (0, -1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, 1)), ((0, -1, 0), (1, 0, 0), (0, 0, -1)),
        ((0, 0, 1), (1, 0, 0), (0, -1, 0)), ((0, 0, -1), (-1, 0, 0), (0, -1, 0)),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for n, u, v in faces:
        base = len(positions)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = tuple(0.5 * n[k] + 0.5 * su * u[k] + 0.5 * sv * v[k] for k in range(3))
            positions.append(p)
            normals.append(n)
            uvs.append(((su + 1) / 2, (sv + 1) / 2))
        # Counter-clockwise seen from outside: u x v points INTO the face for these axes, so the order flips.
        quad = [0, 1, 2, 0, 2, 3]
        p0, p1, p2 = positions[base], positions[base + 1], positions[base + 2]
        e1 = [p1[k] - p0[k] for k in range(3)]
        e2 = [p2[k] - p0[k] for k in range(3)]
        cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if sum(cross[k] * n[k] for k in range(3)) < 0:
            quad = [0, 2, 1, 0, 3, 2]
        indices.extend(base + i for i in quad)
    check_winding(positions, normals, indices)

    g = Gltf()
    g.doc["images"] = [{"uri": "textures/crate_basecolor.png"}, {"uri": "textures/crate_normal.png"},
                       {"uri": "textures/crate_metallic_roughness.png"}]
    g.doc["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
    g.doc["textures"] = [{"sampler": 0, "source": i} for i in range(3)]
    g.doc["materials"] = [{"name": "crate",
                           "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                    "metallicRoughnessTexture": {"index": 2},
                                                    "metallicFactor": 1.0, "roughnessFactor": 1.0},
                           "normalTexture": {"index": 1, "scale": 1.0}}]
    mesh = g.mesh("crate", positions, normals, uvs, indices, 0)
    g.node("crate", mesh)
    g.write_gltf(OUT / "crate.gltf", "crate.bin")


# --- The spheres ----------------------------------------------------------------------


def sphere_geometry(radius=0.5, rings=24, segments=48, centre=(0.0, 0.0, 0.0)):
    """A UV sphere: positions, normals, uvs, indices (counter-clockwise from outside)."""
    positions, normals, uvs, indices = [], [], [], []
    for r in range(rings + 1):
        theta = math.pi * r / rings  # 0 at the north pole
        for s in range(segments + 1):
            phi = 2 * math.pi * s / segments
            n = (math.sin(theta) * math.sin(phi), math.cos(theta), math.sin(theta) * math.cos(phi))
            positions.append(tuple(radius * c + centre[k] for k, c in enumerate(n)))
            normals.append(n)
            uvs.append((s / segments, r / rings))
    for r in range(rings):
        for s in range(segments):
            a = r * (segments + 1) + s
            b = a + segments + 1
            # Skip the degenerate triangles at the poles.
            if r != 0:
                indices.extend((a, b, a + 1))
            if r != rings - 1:
                indices.extend((a + 1, b, b + 1))
    check_winding(positions, normals, indices)
    return positions, normals, uvs, indices


def box_geometry(size, centre):
    """An axis-aligned box: 24 vertices (flat faces), counter-clockwise from outside."""
    positions, normals, uvs, indices = [], [], [], []
    for axis in range(3):
        for sign in (1.0, -1.0):
            n = [0.0, 0.0, 0.0]
            n[axis] = sign
            u = [0.0, 0.0, 0.0]
            u[(axis + 1) % 3] = 1.0
            v = [n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]]
            base = len(positions)
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                positions.append(tuple(centre[k] + 0.5 * size[k] * (n[k] + su * u[k] + sv * v[k]) for k in range(3)))
                normals.append(tuple(n))
                uvs.append(((su + 1) / 2, (sv + 1) / 2))
            indices.extend(base + i for i in (0, 1, 2, 0, 2, 3))
    check_winding(positions, normals, indices)
    return positions, normals, uvs, indices


def spheres():
    positions, normals, uvs, indices = sphere_geometry()

    g = Gltf()
    # From rough plastic to polished gold.
    looks = [((0.8, 0.1, 0.1, 1), 0.0, 0.9), ((0.1, 0.5, 0.8, 1), 0.0, 0.5), ((0.9, 0.9, 0.9, 1), 0.0, 0.15),
             ((0.95, 0.64, 0.54, 1), 1.0, 0.6), ((0.91, 0.92, 0.92, 1), 1.0, 0.35),
             ((0.56, 0.57, 0.58, 1), 1.0, 0.15), ((1.0, 0.77, 0.34, 1), 1.0, 0.25)]
    for i, (colour, metallic, roughness) in enumerate(looks):
        g.doc["materials"].append({"name": "sphere %d" % i,
                                   "pbrMetallicRoughness": {"baseColorFactor": list(colour),
                                                            "metallicFactor": metallic, "roughnessFactor": roughness}})
    # One mesh per material (a glTF primitive names its material), sharing the vertex data.
    pos = g.floats(positions, "VEC3", with_bounds=True)
    nrm = g.floats(normals, "VEC3")
    tex = g.floats(uvs, "VEC2")
    idx = g.indices(indices)
    for i in range(len(looks)):
        g.doc["meshes"].append({"name": "sphere %d" % i, "primitives": [
            {"attributes": {"POSITION": pos, "NORMAL": nrm, "TEXCOORD_0": tex}, "indices": idx, "material": i}]})
        g.node("sphere %d" % i, i, translation=[(i - 3) * 1.25, 0.0, 0.0])
    g.write_glb(OUT / "spheres.glb")


# --- The lamp ---------------------------------------------------------------------------


def lamp():
    g = Gltf()
    g.doc["extensionsUsed"] = ["KHR_materials_emissive_strength"]
    g.doc["materials"] = [
        {"name": "post", "pbrMetallicRoughness": {"baseColorFactor": [0.06, 0.06, 0.07, 1.0],
                                                  "metallicFactor": 0.8, "roughnessFactor": 0.4}},
        # glTF caps emissiveFactor at 1; the extension multiplies it, for a light brighter than white.
        {"name": "bulb", "emissiveFactor": [1.0, 0.62, 0.28],
         "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 12.0}},
         "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.9, 0.8, 1.0], "metallicFactor": 0.0,
                                  "roughnessFactor": 0.3}},
    ]
    post = g.mesh("post", *box_geometry((0.08, 1.5, 0.08), (0.0, 0.75, 0.0)), 0)
    bulb = g.mesh("bulb", *sphere_geometry(radius=0.14, rings=16, segments=32, centre=(0.0, 1.62, 0.0)), 1)
    g.node("post", post)
    g.node("bulb", bulb)
    g.write_glb(OUT / "lamp.glb")


# --- The floor ------------------------------------------------------------------------


def floor():
    size = 128

    def tile(x, y):
        light = (x // 64 + y // 64) % 2 == 0
        edge = x % 64 in (0, 63) or y % 64 in (0, 63)
        v = 40 if edge else (150 if light else 95)
        speck = 6 * math.sin(x * 1.7) * math.cos(y * 2.3)
        return bytes(clamp_byte(c + speck) for c in (v, v * 0.98, v * 0.93))

    write_png(OUT / "textures" / "floor_tiles.png", size, size, tile)
    half, repeat = 12.0, 12.0
    positions = [(-half, 0, half), (half, 0, half), (half, 0, -half), (-half, 0, -half)]
    normals = [(0, 1, 0)] * 4
    uvs = [(0, repeat), (repeat, repeat), (repeat, 0), (0, 0)]
    indices = [0, 1, 2, 0, 2, 3]
    check_winding(positions, normals, indices)

    g = Gltf()
    g.doc["images"] = [{"uri": "textures/floor_tiles.png"}]
    g.doc["textures"] = [{"source": 0}]
    g.doc["materials"] = [{"name": "floor", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                                    "metallicFactor": 0.0, "roughnessFactor": 0.8}}]
    g.node("floor", g.mesh("floor", positions, normals, uvs, indices, 0))
    g.write_gltf(OUT / "floor.gltf", "floor.bin")


if __name__ == "__main__":
    (OUT / "textures").mkdir(parents=True, exist_ok=True)
    crate()
    spheres()
    lamp()
    floor()
    for path in sorted(OUT.rglob("*")):
        if path.is_file():
            print("%8d  %s" % (path.stat().st_size, path.relative_to(OUT.parent.parent.parent)))
