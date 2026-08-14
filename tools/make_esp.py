#!/usr/bin/env python3
"""Build ScavengerSense.esp from scratch.

Emits an ESL-flagged plugin containing:
  * 128 EFSH (Effect Shader) records, local FormID 0x000800 .. 0x00087F,
    EditorID SS_SenseFX_000 .. SS_SenseFX_127
  * 1 IMAD (Image Space Adapter) record, local FormID 0x000880, EditorID SS_SenseTint

On disk these carry the plugin's own file index (0x01, see SELF_INDEX); the DLL
looks them up by the *local* id via TESDataHandler::LookupForm, which composes
the runtime id from the load order.

Every field is written explicitly so the file is self-documenting. Offsets for
the EFSH DATA subrecord were verified against CommonLibSSE-NG's
RE::EffectShaderData; the IMAD layout against xEdit's wbDefinitionsTES5.pas.
"""

import struct

SHADER_COUNT = 128

# FormIDs as stored in the file carry the *file index* of whichever plugin
# defines them. Index 0x00 is our first master (Skyrim.esm); records this
# plugin introduces itself must use the next index up - 0x01, because we
# declare exactly one master. Authoring them at 0x00xxxxxx makes the game read
# them as overrides of Skyrim.esm forms that do not exist, and the records are
# then unreachable through this plugin.
SELF_INDEX = 0x01000000

SHADER_BASE = SELF_INDEX | 0x000800
IMOD_FORMID = SELF_INDEX | 0x000880

# ---------------------------------------------------------------- primitives


def sub(sig: bytes, payload: bytes) -> bytes:
    assert len(sig) == 4
    return sig + struct.pack("<H", len(payload)) + payload


def zstr(text: str) -> bytes:
    return text.encode("cp1252") + b"\0"


def rec(sig: bytes, formid: int, body: bytes, flags: int = 0) -> bytes:
    # size, flags, formid, version control, form version, unknown
    return (
        sig
        + struct.pack("<I", len(body))
        + struct.pack("<I", flags)
        + struct.pack("<I", formid)
        + struct.pack("<I", 0)
        + struct.pack("<H", 44)
        + struct.pack("<H", 0)
        + body
    )


def grup(label: bytes, records: bytes) -> bytes:
    # top level group: size includes the 24 byte header
    return (
        b"GRUP"
        + struct.pack("<I", len(records) + 24)
        + label
        + struct.pack("<i", 0)
        + struct.pack("<I", 0)
        + struct.pack("<H", 0)
        + struct.pack("<H", 0)
        + records
    )


def rgba(r: int, g: int, b: int, a: int = 0) -> bytes:
    return bytes((r, g, b, a))


# ------------------------------------------------------------------- EFSH


def effect_shader_data(colour=(0xFF, 0xD2, 0x4A)) -> bytes:
    """400 byte DATA subrecord for a membrane-only 'sense' shader.

    IMPORTANT: these are *file* offsets, taken from xEdit's EFSH definition.
    They are NOT the offsets of CommonLibSSE's RE::EffectShaderData - the two
    diverge after 0x0F4 because the record stores Addon Models and Ambient
    Sound as 4 byte FormIDs where the runtime struct holds 8 byte pointers.
    Using runtime offsets here silently lands values in the wrong fields.

    The runtime rewrites colour, Z-test, fall-off and the alpha envelope on
    every use; everything else has to be right in the file.
    """
    d = bytearray(400)

    def u32(off, val):
        struct.pack_into("<I", d, off, val)

    def f32(off, val):
        struct.pack_into("<f", d, off, val)

    def col(off, value):
        d[off : off + 4] = rgba(*value)

    r, g, b = colour
    tint = (r, g, b, 0)

    # 0x000 flags (the record carries them twice, here and at 0x180):
    #   0x02 GreyscaleToColor   - fill texture luminance indexes the colour keys
    #   0x04 GreyscaleToAlpha   - fill texture luminance drives alpha
    #   0x08 DisableParticleShader
    #   0x40 IgnoreTexAlpha
    flags = 0x02 | 0x04 | 0x08 | 0x40
    u32(0x000, flags)

    # membrane blend state: SRCALPHA, ADD, dest ONE -> additive glow.
    # Z test kAlways(8) is what lets it draw through geometry.
    u32(0x004, 5)  # source blend  = D3DBLEND_SRCALPHA
    u32(0x008, 1)  # blend op      = D3DBLENDOP_ADD
    u32(0x00C, 8)  # z test        = D3DCMP_ALWAYS
    u32(0x05C, 2)  # dest blend    = D3DBLEND_ONE

    # fill texture effect
    col(0x010, tint)
    f32(0x014, 0.15)  # alpha fade in time
    f32(0x018, 4.00)  # full alpha time
    f32(0x01C, 1.00)  # alpha fade out time
    f32(0x020, 0.55)  # persistent alpha ratio
    f32(0x024, 0.35)  # alpha pulse amplitude
    f32(0x028, 1.20)  # alpha pulse frequency
    f32(0x02C, 0.00)  # texture animation speed U
    f32(0x030, 0.00)  # texture animation speed V

    # edge effect - the outline that carries the category colour
    f32(0x034, 1.60)  # fall off
    col(0x038, tint)  # colour
    f32(0x03C, 0.15)  # alpha fade in time
    f32(0x040, 4.00)  # full alpha time
    f32(0x044, 1.00)  # alpha fade out time
    f32(0x048, 0.90)  # persistent alpha ratio
    f32(0x04C, 0.35)  # alpha pulse amplitude
    f32(0x050, 1.20)  # alpha pulse frequency

    f32(0x054, 1.0)  # fill full alpha ratio
    f32(0x058, 1.0)  # edge full alpha ratio

    # particle shader is switched off by the flag, but keep the blend sane
    u32(0x060, 5)
    u32(0x064, 1)
    u32(0x068, 4)
    u32(0x06C, 2)

    # particle colour keys (unused while the particle shader is off)
    col(0x0BC, (0xFF, 0xFF, 0xFF, 0))
    col(0x0C0, (0xFF, 0xFF, 0xFF, 0))
    col(0x0C4, (0x86, 0x86, 0x86, 0))
    f32(0x0C8, 0.00)  # colour key 1 alpha
    f32(0x0CC, 0.25)  # colour key 2 alpha
    f32(0x0D0, 0.00)  # colour key 3 alpha
    f32(0x0D4, 0.00)  # colour key 1 time
    f32(0x0D8, 0.35)  # colour key 2 time
    f32(0x0DC, 1.00)  # colour key 3 time

    u32(0x0F4, 0)  # addon models: NULL

    # holes: inert because NAM7 (holes texture) is empty, but a start value of
    # 0 with an end value of 0 is the "punch nothing out" configuration
    f32(0x0F8, 0.0)    # holes start time
    f32(0x0FC, 10.0)   # holes end time
    f32(0x100, 255.0)  # holes start val
    f32(0x104, 0.0)    # holes end val

    f32(0x108, 0.0)                    # edge width in alpha units
    col(0x10C, (0xFF, 0xFF, 0xFF, 0))  # secondary edge colour
    f32(0x110, 0.0)                    # explosion wind speed

    # Texture tiling counts. These are INTEGERS, and a count of zero gives
    # degenerate UVs - the membrane samples nothing and the whole effect is
    # invisible. This is the field that has to be right.
    u32(0x114, 2)  # texture count U
    u32(0x118, 2)  # texture count V

    # addon model animation (no addon models, but mirror sane values)
    f32(0x11C, 0.1)  # fade in time
    f32(0x120, 0.1)  # fade out time
    f32(0x124, 1.0)  # scale start
    f32(0x128, 1.0)  # scale end
    f32(0x12C, 1.0)  # scale in time
    f32(0x130, 1.0)  # scale out time

    u32(0x134, 0)  # ambient sound: NULL

    # Membrane greyscale-to-colour gradient. All three keys the same gives a
    # flat category colour instead of a gradient. The scales must be 1.0 -
    # at 0.0 the colour output is scaled away to nothing.
    col(0x138, tint)  # fill colour key 2
    col(0x13C, tint)  # fill colour key 3
    f32(0x140, 1.0)   # colour key 1 scale
    f32(0x144, 1.0)   # colour key 2 scale
    f32(0x148, 1.0)   # colour key 3 scale
    f32(0x14C, 0.0)   # colour key 1 time
    f32(0x150, 0.5)   # colour key 2 time
    f32(0x154, 1.5)   # colour key 3 time

    f32(0x158, 1.0)  # colour scale
    f32(0x15C, 0.0)  # birth position offset
    f32(0x160, 0.0)  # birth position offset range

    # trailing block: the real flags word, then the fill texture scale
    u32(0x180, flags)
    f32(0x184, 3.0)
    f32(0x188, 2.0)
    u32(0x18C, 0)

    return bytes(d)


def effect_shader(index: int) -> bytes:
    body = b"".join(
        [
            sub(b"EDID", zstr(f"SS_SenseFX_{index:03d}")),
            sub(b"ICON", zstr(r"Effects\EnchFlameProject01.dds")),  # fill texture
            sub(b"ICO2", b"\0"),  # particle texture (unused)
            sub(b"NAM7", b"\0"),  # holes texture (unused)
            sub(b"NAM8", zstr(r"Effects\Gradients\GradAmbFog01.dds")),  # membrane palette
            sub(b"NAM9", b"\0"),  # particle palette (unused)
            sub(b"DATA", effect_shader_data()),
        ]
    )
    return rec(b"EFSH", SHADER_BASE + index, body)


# ------------------------------------------------------------------- IMAD

# The DNAM block is a table of *key counts*, not values: for every animated
# channel it says how many (time, value) pairs live in the matching sub record.
# 17 HDR entries (14 HDR + 3 bloom) then 4 cinematic entries, each a pair of
# u32 counts (mult, add).

TINT_KEYS = 4  # (0, neutral) -> (fade in, target) -> (hold, target) -> (1, neutral)


def interpolator(keys) -> bytes:
    return b"".join(struct.pack("<ff", t, v) for t, v in keys)


def imad_dnam(duration: float) -> bytes:
    d = bytearray(244)
    struct.pack_into("<I", d, 0x00, 1)  # animatable = true
    struct.pack_into("<f", d, 0x04, duration)

    # HDR + bloom: 17 pairs of counts, all zero (offsets 0x08 .. 0x8F)

    # cinematic block starts at 0x08 + 17*8 = 0x90
    cine = 0x90
    struct.pack_into("<I", d, cine + 0x00, TINT_KEYS)  # saturation mult count
    struct.pack_into("<I", d, cine + 0x04, 0)  # saturation add count
    struct.pack_into("<I", d, cine + 0x08, TINT_KEYS)  # brightness mult count
    struct.pack_into("<I", d, cine + 0x0C, 0)
    struct.pack_into("<I", d, cine + 0x10, TINT_KEYS)  # contrast mult count
    struct.pack_into("<I", d, cine + 0x14, 0)
    struct.pack_into("<I", d, cine + 0x18, 0)  # unused
    struct.pack_into("<I", d, cine + 0x1C, 0)

    # tail block at 0x08 + 17*8 + 4*8 = 0xB0, everything left at zero except
    # the radial blur centre which should sit in the middle of the screen
    struct.pack_into("<f", d, 0xB0 + 0x1C, 0.5)  # radial blur centre X
    struct.pack_into("<f", d, 0xB0 + 0x20, 0.5)  # radial blur centre Y

    assert len(d) == 244
    return bytes(d)


def imad(duration=6.0, saturation=0.25, brightness=0.85, contrast=1.15) -> bytes:
    def envelope(target):
        return [
            (0.00 * duration, 1.0),
            (0.10 * duration, target),
            (0.88 * duration, target),
            (1.00 * duration, 1.0),
        ]

    body = b"".join(
        [
            sub(b"EDID", zstr("SS_SenseTint")),
            sub(b"DNAM", imad_dnam(duration)),
            sub(b"TNAM", b""),  # tint colour interpolator - required, empty
            sub(b"NAM3", b""),  # fade colour interpolator - required, empty
            sub(b"RNAM", b""),  # radial blur strength
            sub(b"SNAM", b""),  # radial blur ramp up
            sub(b"UNAM", b""),  # radial blur start
            sub(b"NAM1", b""),  # radial blur ramp down
            sub(b"NAM2", b""),  # radial blur down start
            sub(b"WNAM", b""),  # depth of field strength
            sub(b"XNAM", b""),  # depth of field distance
            sub(b"YNAM", b""),  # depth of field range
            # cinematic multipliers; signature is a raw byte followed by 'IAD'
            sub(b"\x11IAD", interpolator(envelope(saturation))),
            sub(b"\x12IAD", interpolator(envelope(brightness))),
            sub(b"\x13IAD", interpolator(envelope(contrast))),
        ]
    )
    return rec(b"IMAD", IMOD_FORMID, body)


# ------------------------------------------------------------------- header


def build() -> bytes:
    shaders = b"".join(effect_shader(i) for i in range(SHADER_COUNT))
    groups = grup(b"EFSH", shaders) + grup(b"IMAD", imad())

    header_body = b"".join(
        [
            sub(
                b"HEDR",
                # version, record count (including TES4), next free object ID
                struct.pack("<fiI", 1.71, SHADER_COUNT + 2, (IMOD_FORMID & 0xFFFFFF) + 0x10),
            ),
            sub(b"CNAM", zstr("KShakes")),
            sub(b"SNAM", zstr("Scavenger Sense - radius highlight shaders and tint")),
            sub(b"MAST", zstr("Skyrim.esm")),
            sub(b"DATA", struct.pack("<Q", 0)),
            sub(b"INTV", struct.pack("<I", 1)),
        ]
    )

    # 0x00000200 = ESL (light master) so the plugin costs no load order slot
    header = rec(b"TES4", 0, header_body, flags=0x00000200)
    return header + groups


if __name__ == "__main__":
    data = build()
    with open("ScavengerSense.esp", "wb") as handle:
        handle.write(data)
    print(f"wrote ScavengerSense.esp, {len(data)} bytes")
