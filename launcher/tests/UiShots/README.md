# evr-ui-shots: the launcher's windows as pictures

Visual QA without a person: opens the launcher's main window in test mode (a fake game tree under `--work`, as
`tests/e2e.ps1` makes it; nothing else is read or written) on one display and every dialog in each of its states
modal over it, as the launcher opens them, and saves each as a PNG. Built with the solution; not shipped.

    tests\UiShots\bin\Release\evr-ui-shots.exe --out <dir> --work <dir> [--screen <n>] [--drag <n>] [--only <text>] [--check]

- `--screen <n>`: the display (`Screen.AllScreens` order) the main window opens on; the dialogs open over it.
- `--drag <n>`: after each shot, the window is dragged to display `<n>` and shot again (Windows changes its scale as
  when a player drags it).
- `--only <text>`: only the shots whose name holds the text.
- `--check`: the exit code is 1 when a shot has a layout fault (`--only controls --check` checks the controls editor).
- `--live-update <folder>`: instead of the shots, the update dialog's own download and install, for real, over a
  release-shaped folder (use a copy: the newest published release replaces it), shot while it downloads and
  installs; what the dialog logged goes to `live-update.txt`.

Names are `<dpi>-open-<shot>.png` (opened on a display of that DPI) and `<dpi>-dragged-<shot>.png` (opened on the
other display, then dragged to one of that DPI). Each PNG has a `.txt` with the window's DPI, font, the bounds of its
buttons, labels and lists, the bounds of its panels (a table's with its row heights), and the layout faults found: a
control cut off by its parent, button text larger than its button, buttons of uneven height in one row, a fixed-size
label too small for its text, a control moved by a hidden one onto a row of another size (a table gives the controls
added without a cell the next cells in turn, skipping hidden ones), and in a sizable window a filling layout that does
not fill it or an empty band below the last row. `<dpi>-summary.txt` lists every shot with its fault count. The faults
are hints; look at every picture.

The shots: the main window's tabs and its bar with "Update to x.y.z..."; its status line with the start watchdog's
two longest warnings and after a crash; the update dialog able to install, unable for each reason (game running, not
a release, folder not writable), downloading, installing, failed, and failed after the files were replaced; the DLSS
download (offer, downloading, failed); the controls editor; the weapon directions; Restore saves; the new profile
name. The `-layout` shots take the controls editor (with and without a VR settings profile), the main window and
Restore saves through the scale changes of displays at 96, 120, 144 and 168 DPI (up and down, then back to the
display's own), a resize smaller and larger, maximise and restore, and the controls editor's notes shown and hidden,
one shot after each. A faked scale change leaves the frame Windows draws at the display's scale, so the window is
then given the client area a real change gives; auto-sized dialogs are left out of these steps (their size follows
the frame).

The DPI is the display's: WinForms scales by the DPI of the display a window is on, and a scale cannot be faked
faithfully (a scale-change message sent to a window on another display rescales the WinForms parts but not the
title bar, check boxes and scroll bars Windows draws, and a DPI-unaware run is stretched by Windows). On the rig
(200 % and 125 % displays):

    evr-ui-shots --out <dir>\200 --work <tmp>\s0 --screen 0 --drag 1
    evr-ui-shots --out <dir>\125 --work <tmp>\s1 --screen 1 --drag 0

The windows open on the desktop and take the focus; `launcher\tests\run-on-hidden-desktop.ps1 -Exe <exe>
-Arguments "<args>"` runs it on a desktop of its own instead, so it can run while the game is tested.
