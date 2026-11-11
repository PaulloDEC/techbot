The fonts Shrapnel draws its interface with, as C arrays so it needs no font
files beside it:

inter_400.h, inter_600.h: Inter Regular and SemiBold (Latin), by The Inter
  Project Authors (https://github.com/rsms/inter), licensed under the SIL Open
  Font License 1.1 (OFL.txt).  From @fontsource/inter 5.3.0, converted from
  WOFF2 to TrueType.

symbols.h, symbols_filled.h, icons.h: Material Symbols Rounded by Google
  (https://github.com/google/material-design-icons), licensed under the Apache
  License 2.0 (LICENSE-material-symbols.txt).  From the material-symbols
  package 0.48.3: the variable font fixed at weight 400, grade 0, optical
  size 20 (outlined; and filled for a few glyphs, moved to F8B0h on) and cut
  down to the glyphs Shrapnel uses.  icons.h names them (ICON_EDIT, ...).
  To add one: tools/make_fonts.py lists the names.
