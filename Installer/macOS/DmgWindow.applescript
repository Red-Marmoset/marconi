-- Copyright (c) 2026 Red Marmoset Studios Ltd.
-- Has Finder lay out the mounted disk image's window and save it: macOS 26 shows a background picture only when
-- Finder set it itself, not from settings another tool wrote. Package.sh runs it with the layout from
-- Tools/dmg_layout.py:
--   osascript DmgWindow.applescript <volume> <window size> <icon size> <text size> <icon y> <app x> <applications x>
-- The picture must already be in the volume's .background folder, as background.tiff.

on run argv
    set volumeName to item 1 of argv
    set windowSize to (item 2 of argv) as integer
    set iconSize to (item 3 of argv) as integer
    set textSize to (item 4 of argv) as integer
    set iconY to (item 5 of argv) as integer
    set appX to (item 6 of argv) as integer
    set applicationsX to (item 7 of argv) as integer

    tell application "Finder"
        tell disk volumeName
            open
            set current view of container window to icon view
            set toolbar visible of container window to false
            set statusbar visible of container window to false
            set the bounds of container window to {200, 120, 200 + windowSize, 120 + windowSize}
            set viewOptions to the icon view options of container window
            set arrangement of viewOptions to not arranged
            set icon size of viewOptions to iconSize
            set text size of viewOptions to textSize
            set background picture of viewOptions to file ".background:background.tiff"
            set position of item "Marconi.app" of container window to {appX, iconY}
            set position of item "Applications" of container window to {applicationsX, iconY}
            update without registering applications
            delay 2
            close
        end tell
    end tell
end run
