# Copyright (c) 2026 Red Marmoset Studios Ltd.
# The macOS disk image window's layout, in points, shared by Tools/make_installer_art.py (which draws the
# background) and Package.sh (which has Finder place the icons, through Installer/macOS/DmgWindow.applescript), so
# the arrow always sits between them.

WINDOW_SIZE = 600            # the window's content is square
ICON_SIZE = 108              # large, but leaving room for the name above
TEXT_SIZE = 14               # icon labels
ICON_Y = 300                 # icon centres, from the top
APP_X = 160                  # Marconi.app's centre
APPLICATIONS_X = WINDOW_SIZE - APP_X
