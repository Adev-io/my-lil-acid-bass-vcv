#!/usr/bin/env python3
"""panel.py: writes res/AcidBass.svg, res/AcidVoice.svg and src/layout.hpp from two layout tables.

Labels become paths (DejaVu Sans from the Rack install, or the font named by RACK_FONT), so the SVGs have no <text>,
as the VCV panel guide asks. The widgets read the same tables through layout.hpp, so the art and the components
cannot drift apart. Run from anywhere: py -3.11 tools/panel.py   (needs fontTools).
Author: Ari Klopfer / A_Development. MIT
"""
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

H = 128.5
INK, INK2 = '#16161a', '#202027'
LIME, MAGENTA, BLUE, WHITE, DIM = '#c8ff3a', '#ff3d9a', '#5ac8e6', '#f2f2f4', '#8d8d96'
FONT = os.environ.get('RACK_FONT', r'C:\Program Files\VCV\Rack2Free\res\fonts\DejaVuSans.ttf')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# kinds: hero Rogan3PS, big Rogan2PS, knob Rogan1PS (white or blue), tiny BefacoTinyKnob, trim Trimpot,
# latch VCVLightLatch, button VCVButton, light MediumLight, and jacks: play (lime ring), cv (magenta ring),
# seq (blue ring), out (lime pad)
RADIUS = {'hero': 8.8, 'big': 7.35, 'knob': 6.7, 'tiny': 4.3, 'trim': 3.0, 'latch': 3.1, 'button': 3.1,
          'play': 4.0, 'cv': 4.0, 'seq': 4.0, 'out': 4.0, 'light': 1.5}
RING = {'play': LIME, 'cv': MAGENTA, 'seq': BLUE}
LABEL_SIZE = {'hero': 3.4, 'big': 2.8, 'knob': 2.8, 'tiny': 2.4, 'trim': 2.2, 'latch': 2.4, 'button': 2.4,
              'play': 2.4, 'cv': 2.4, 'seq': 2.4, 'out': 2.6}

font = TTFont(FONT)
glyphs, cmap, upm = font.getGlyphSet(), font.getBestCmap(), font['head'].unitsPerEm


def text(s, x, y, size, fill, anchor='middle', stroke=0.0):
    """One <path> for the string: baseline at y, em size in mm, optional stroke for a bolder face."""
    k = size / upm
    width = sum(glyphs[cmap[ord(c)]].width for c in s) * k
    pen_x = x - width / 2 if anchor == 'middle' else x - width if anchor == 'end' else x
    d = []
    for c in s:
        g = glyphs[cmap[ord(c)]]
        pen = SVGPathPen(glyphs)
        g.draw(TransformPen(pen, (k, 0, 0, -k, pen_x, y)))
        d.append(pen.getCommands())
        pen_x += g.width * k
    bold = ' stroke="%s" stroke-width="%g" stroke-linejoin="round"' % (fill, stroke) if stroke else ''
    return '<path fill="%s"%s d="%s"/>' % (fill, bold, ' '.join(d))


def mascot(cx, cy, r):
    """A little acid face: magenta disc, dot eyes, a wavy mouth."""
    e, m = r * 0.37, r * 0.29
    return ''.join([
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx, cy, r, MAGENTA),
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx - e, cy - r * 0.32, r * 0.145, INK),
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx + e, cy - r * 0.32, r * 0.145, INK),
        '<path d="M %g %g q %g %g %g 0 q %g %g %g 0" fill="none" stroke="%s" stroke-width="%g" stroke-linecap="round"/>'
        % (cx - 2 * m, cy + r * 0.29, m, r * 0.45, 2 * m, m, -r * 0.45, 2 * m, INK, r * 0.16),
    ])


def panel(W, title_x, mascot_x, layout, extras, pad_r, band=8.6, title_size=4.3, title_y=6.3, below=True, title='MY LIL ACID BASS'):
    """The common frame: ink panel, lime title band, mascot, brand; then the extras and the labelled components.
    Labels sit below the components (or above, below=False), one baseline per row set by the row's biggest part."""
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, H, INK),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, band, LIME),
        text(title, title_x, title_y, title_size, INK, stroke=title_size * 0.09),
        mascot(mascot_x, band / 2, band * 0.36),
    ]
    out += extras
    rowmax = {}
    for _, kind, x, y, label in layout:
        if kind not in ('out', 'light'):
            rowmax[y] = max(rowmax.get(y, 0), RADIUS[kind])
    for _, kind, x, y, label in layout:
        r = RADIUS[kind]
        if kind in RING:
            out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="%s" stroke-width="0.6"/>' % (x, y, r + 1.3, RING[kind]))
        elif kind == 'out':
            out.append('<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (x, y, pad_r, LIME))
        if not label:
            continue
        colour, stroke = (LIME, 0.3) if kind == 'hero' else (LIME, 0.25) if kind == 'out' else (WHITE, 0.18)
        reach = pad_r if kind == 'out' else rowmax[y]
        ly = y + reach + 2.8 if below else y - reach - 2.3
        out.append(text(label, x, ly, LABEL_SIZE[kind], colour, stroke=stroke))
    out.append(text('A_DEVELOPMENT', W / 2, 125.6, 2.4, DIM))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------------------------------------------------------- Acid Bass, 40 HP
BW = 5.08 * 40
R1, R2, R3 = 70, 87, 105
TRIM_X = (11, 21, 31, 45, 55, 65, 75, 89, 99, 109, 119, 129, 139)
TRIM_IDS = ('FATK', 'FSUS', 'FREL', 'AATK', 'ADEC', 'ASUS', 'AREL', 'HP', 'KFOL', 'MIND', 'PW', 'LFOR', 'LFOPW')
TRIM_LABELS = ('F.ATK', 'F.SUS', 'F.REL', 'A.ATK', 'A.DEC', 'A.SUS', 'A.REL', 'HP', 'KEY', 'MIN.D', 'PW', 'LFO', 'LFO>PW')
BASS = [
    ('TUNE_PARAM', 'knob', 12, R1, 'TUNE'), ('CUTOFF_PARAM', 'hero', 31, R1, 'CUTOFF'), ('RES_PARAM', 'knob', 49.5, R1, 'RES'),
    ('ENVMOD_PARAM', 'knob', 65.5, R1, 'ENV MOD'), ('DECAY_PARAM', 'knob', 81.5, R1, 'DECAY'), ('ACCENT_PARAM', 'knob', 97.5, R1, 'ACCENT'),
    ('VOLUME_PARAM', 'knob', 113.5, R1, 'VOLUME'), ('WAVE_PARAM', 'latch', 128, R1, 'SQUARE'),
    ('TEMPO_PARAM', 'knob', 150, R1, 'TEMPO'), ('SWING_PARAM', 'tiny', 168, R1, 'SWING'), ('GATE_PARAM', 'tiny', 184, R1, 'GATE'),
] + [(i + '_PARAM', 'trim', x, R2, l) for i, x, l in zip(TRIM_IDS, TRIM_X, TRIM_LABELS)] + [
    ('PATTERN_PARAM', 'tiny', 153, R2, 'PATTERN'), ('LENGTH_PARAM', 'tiny', 168, R2, 'LENGTH'),
    ('RUN_PARAM', 'latch', 181, R2, 'RUN'), ('RESET_PARAM', 'button', 193, R2, 'RESET'),
    ('CLOCK_INPUT', 'seq', 10, R3, 'CLOCK'), ('RESET_INPUT', 'seq', 21, R3, 'RESET'), ('PATTERN_INPUT', 'seq', 32, R3, 'PAT'),
    ('VOCT_INPUT', 'play', 45, R3, 'V/OCT'), ('GATE_INPUT', 'play', 56, R3, 'GATE'), ('ACCENT_INPUT', 'play', 67, R3, 'ACC'),
    ('CUTOFF_CV_INPUT', 'cv', 80, R3, 'CUT'), ('RES_CV_INPUT', 'cv', 91, R3, 'RES'), ('ENVMOD_CV_INPUT', 'cv', 102, R3, 'ENV'),
    ('DECAY_CV_INPUT', 'cv', 113, R3, 'DEC'), ('ACCENT_CV_INPUT', 'cv', 124, R3, 'ACC'),
    ('SEQ_VOCT_OUTPUT', 'seq', 136, R3, 'V/OCT'), ('SEQ_GATE_OUTPUT', 'seq', 147, R3, 'GATE'), ('SEQ_ACCENT_OUTPUT', 'seq', 158, R3, 'ACC'),
    ('FENV_OUTPUT', 'cv', 171, R3, 'FILT'), ('AENV_OUTPUT', 'cv', 182, R3, 'AMP'),
    ('AUDIO_OUTPUT', 'out', 193, R3, 'AUDIO'),
]
BASS_EXTRAS = [
    '<rect x="4.5" y="9.5" width="194.2" height="51" rx="1.5" fill="none" stroke="%s" stroke-width="0.5"/>' % LIME,   # frames the step grid
    '<rect x="3.5" y="95.5" width="196.2" height="21" rx="2" fill="%s"/>' % INK2,                                     # jack zone
    text('SEQ IN', 21, 98.4, 2.6, BLUE, stroke=0.2), text('PLAY IN', 56, 98.4, 2.6, LIME, stroke=0.2),
    text('CV IN', 102, 98.4, 2.6, MAGENTA, stroke=0.2), text('SEQ OUT', 147, 98.4, 2.6, BLUE, stroke=0.2), text('ENV OUT', 176.5, 98.4, 2.6, MAGENTA, stroke=0.2),
    text('OUT', 193, 98.4, 2.6, LIME, stroke=0.2),
]

# ---------------------------------------------------------------------------------------------------------------- Acid Voice, 14 HP
VW = 5.08 * 14
K = (13.5, 35.56, 57.6)          # knob columns
J = (10.5, 27.2, 43.9, 60.6)     # jack columns
LIGHT_DX = 6.5                   # a status light sits this far right of its jack
VOICE = [
    ('RES_PARAM', 'big', K[0], 30, 'RES'), ('CUTOFF_PARAM', 'hero', K[1], 30, 'CUTOFF'), ('ENVMOD_PARAM', 'big', K[2], 30, 'ENV MOD'),
    ('DECAY_PARAM', 'knob', K[0], 58.5, 'DECAY'), ('ACCENT_PARAM', 'knob', K[1], 58.5, 'ACCENT'), ('VOLUME_PARAM', 'knob', K[2], 58.5, 'VOL'),
    ('TUNE_PARAM', 'trim', K[0], 76, 'TUNE'), ('WAVE_PARAM', 'latch', K[1], 76, 'SQUARE'), ('CUTOFF_CV_PARAM', 'trim', K[2], 76, 'CUT CV'),
    ('VOCT_INPUT', 'play', J[0], 95.5, 'V/OCT'), ('GATE_INPUT', 'play', J[1], 95.5, 'GATE'),
    ('ACCENT_INPUT', 'play', J[2], 95.5, 'ACCENT'), ('SLIDE_INPUT', 'play', J[3], 95.5, 'SLIDE'),
    ('GATE_LIGHT', 'light', J[1] + LIGHT_DX, 95.5, ''), ('ACCENT_LIGHT', 'light', J[2] + LIGHT_DX, 95.5, ''),
    ('SLIDE_LIGHT', 'light', J[3] + LIGHT_DX, 95.5, ''),
    ('CUTOFF_CV_INPUT', 'cv', J[0], 112, 'CUT CV'), ('RES_CV_INPUT', 'cv', J[1], 112, 'RES CV'),
    ('AUDIO_OUTPUT', 'out', K[2], 112, 'OUT'),
]
VOICE_EXTRAS = [
    '<rect x="3.5" y="86" width="%g" height="35.5" rx="2.5" fill="%s"/>' % (VW - 7, INK2),   # play zone
    '<rect x="3.5" y="84.1" width="%g" height="0.5" fill="%s"/>' % (VW - 7, MAGENTA),
    '<rect x="29" y="82" width="13" height="4.6" fill="%s"/>' % INK,
    text('PLAY', VW / 2, 85.6, 3.0, MAGENTA, stroke=0.2),
]


def header():
    lines = ['// layout.hpp: generated by tools/panel.py, do not edit. Component centres in mm.', '#pragma once', 'namespace layout {']
    for name, table in (('bass', BASS), ('voice', VOICE)):
        lines.append('namespace %s {' % name)
        lines += ['constexpr float %s[2] = {%.2ff, %.2ff};' % (i, x, y) for i, _, x, y, _ in table]
        mx, band = {'bass': (186, 8.6), 'voice': (65.5, 15.5)}[name]
        lines.append('constexpr float FACE[3] = {%.2ff, %.2ff, %.3ff};   // mascot centre x, y, radius (TalkingFace)' % (mx, band / 2, band * 0.36))
        lines.append('}')
    lines += ['}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    files = {
        os.path.join('res', 'AcidBass.svg'): panel(BW, 101.6, 186, BASS, BASS_EXTRAS, pad_r=5.6),
        os.path.join('res', 'AcidVoice.svg'): panel(VW, 31, 65.5, VOICE, VOICE_EXTRAS, pad_r=7, band=15.5, title_size=4.4, title_y=10.6, below=False, title='MY LIL ACID VOICE'),
        os.path.join('src', 'layout.hpp'): header(),
    }
    for rel, body in files.items():
        with open(os.path.join(ROOT, rel), 'w', encoding='utf-8', newline='\n') as f:
            f.write(body)
    print('wrote', ', '.join(files), '(%d + %d components)' % (len(BASS), len(VOICE)))
