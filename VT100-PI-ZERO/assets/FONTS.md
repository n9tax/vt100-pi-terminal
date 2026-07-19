# Bundled fonts

These TrueType fonts are bundled with VT100-PI-ZERO and selectable from the
Setup menu (Ctrl+F3 → Font). All are under open, redistributable licenses.
Full license texts are available at each font's upstream source.

| Font | License | Source |
|---|---|---|
| DejaVu Sans Mono (default) | DejaVu / Bitstream Vera (permissive) | https://dejavu-fonts.github.io/ |
| Liberation Mono | SIL Open Font License 1.1 | https://github.com/liberationfonts/liberation-fonts |
| Noto Sans Mono | SIL Open Font License 1.1 | https://fonts.google.com/noto |
| Hack | MIT + Bitstream Vera (Hack Open Font License) | https://github.com/source-foundry/Hack |
| JetBrains Mono | SIL Open Font License 1.1 | https://github.com/JetBrains/JetBrainsMono |
| Fira Code | SIL Open Font License 1.1 | https://github.com/tonsky/FiraCode |
| Source Code Pro | SIL Open Font License 1.1 | https://github.com/adobe-fonts/source-code-pro |
| VT323 | SIL Open Font License 1.1 | https://fonts.google.com/specimen/VT323 |

Notes:
- Fira Code is the variable font from Google Fonts; it renders at its default
  (Light) instance since the glyph atlas doesn't set variation axes.
- The renderer ignores programming ligatures (Fira Code, JetBrains Mono) — glyph
  substitution needs a shaping engine, which this atlas doesn't use.

To add another font: drop its `.ttf` here and add one line to `EXTRA[]` in
`../src/video/fonts.c`. Fonts not redistributable here (e.g. Monaco) can still be
used by pointing `font = /absolute/path.ttf` in `~/.config/vt100-pi/vt100.conf`.
