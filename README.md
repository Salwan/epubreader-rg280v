EPUB Book Reader for RG280V
=========================================

Would be awesome if I can read epub books on my RG280V.

## Implementation status

**Milestone 1: accepted on RG280V / Adam v2.1; all device checklist items passed.**

The diagnostic OPK exercises native display patterns, both aspect-correct font
paths, button events, six themes, MP3/chirp audio, and ALSA volume persistence.
Version `0.1.1` incorporates the device feedback: a two-native-row progress bar,
warmer amber, and a warm light-background `warm2` palette. The selected reader
path is 640×480 with horizontal 2:1 box filtering; UI bitmap text is 5×14 native.

- Build: `bash scripts/build-opk.sh`
- Artifact: `dist/epubreader-diagnostic-m1-0.1.1.opk`
- [Build and host verification](docs/BUILD.md)
- [Milestone 1 verification results and artifact checksum](docs/VERIFICATION.md)
- [Installation, controls, and visual follow-up](docs/DEVICE-TEST-0.1.1.md)
- [Passed checklist and device feedback](docs/DEVICE-TEST.md)
- [Pinned dependencies and licenses](third_party/README.md)

The feature design below remains the baseline for the complete reader.

It has a strange chipset, but there are already many apps made for it using the platform name "OpenDingux" specifically for an older Anbernic device RG350.

## Resources for development

* [OpenDingux Linux code repositories](https://github.com/OpenDingux)
* [List of software packages for OpenDingux with source code for many](https://github.com/retrogamehandheld/OpenDingux)
* [Apps for RG350](https://github.com/RafaVico/RG350_APPREPOSITORY)

These apps are useful for reference and they work well on the RG280V:

* [Commander file manager](https://github.com/od-contrib/commander), uses a nice native-resolution font
* [GMU Music Player](https://github.com/denis-n-kuznetsov/gmu), supports mp3 playback
* PdfViewer, source downloaded in: `~/dev/opendingux.pdfviewer/pdfviewer.sourcecode.Hardyx/`

## App Design

The app is distributed as one OpenDingux `.opk` package and targets Adam v2.1 on the RG280V.

App drawing should look right on the unusual RG280V screen: the native framebuffer is 320x480, but each square pixel occupies two vertical framebuffer pixels. The aspect-correct logical resolution is therefore 320x240. Pixel-specific indicators below are measured in **native framebuffer pixels**, not logical pixels.

The initial reading target is English-language prose in unprotected, reflowable EPUBs.

### Content folders and saved state

* EPUB books are found in `$HOME/books` by default.
* MP3 music is found in `$HOME/music` by default.
* Both folder paths can be changed in the app settings.
* Settings, reading positions, completion status, and resume state are saved under `$HOME/.epubreader/`.
* On a fresh launch, the app opens the Books List. If the app was exited while reading, the next launch resumes that book at its last saved position. If it was exited from the Books List, the app returns to the list.

Default theme is dark. Default font size is medium. Both are remembered when changed.

At any point during the app, pressing select + start exits.

### Books List screen

Uses 5×14-native-pixel bitmap text (small and sharp) to display the list of books.

Lists all the books found in the folder with the status of each book (percentage of completion) and a checkmark if the book has been read.

Shows current active theme from available themes (remembers which one was set last): dark (default), warm, amber, matrix, light, and warm2 (very warm light background with dark text).

Themes can be changed by pressing R2 and L2 buttons.

Shows current active book font size (remembers last set): small, medium (default), large.

Book rendering font size can be changed by toggled through presets by pressing Y button. This is the only place font size is changeable (in the books list) because they'll change how many pages books have.

Books are sorted alphabetically. Finished books are listed at the end. When the list overflows the screen, a scrollbar appears at the right and is 2 native framebuffer pixels wide.

* A cursor that highlights the current book is used to select which book to open.
* dpad up and down to move up and down.
* dpad right and left to move entire pages (page up and down basically) or if it's a small list, just jumps to first and last page respectively.
* moving between selections plays a very brief warm chirp for better feel.
* holding dpad up or down behaves like expected: moves one book up/down, then after a short delay key repeat kicks in and it quickly moves through books.
* book can be opened by pressing A button.

### Book Reader screen

The reading experience is focused with only these two components shown during reading:

* the actual book text rendered using truetype utf-8 font at the preset size, at 640×480 and horizontally box-filtered to 320×480
* a 2-native-pixel-high bottom bar indicating the current reading position; use a low-contrast color so it does not stand out.

in-book hyperlinks are rendered but aren't clickable/interactable (minimalism is the point).

Theme can be changed by pressing R2 and L2 buttons, but font-size is unchangeable from the book reading screen.

* dpad left or L1 turns to the previous page.
* dpad right or R1 turns to the next page.
* Pressing select opens or closes the position slider. While it is open, dpad left and right adjust the target by 1%, dpad up and down adjust it by 10%, A jumps to the selected position, and B cancels. Pressing X cancels the slider and opens the popup menu.

Pressing X brings up the popup menu and book header panel:

* Book header panel occupies the top of the screen and shows the book info (title, file, page count, etc).
* Popup menu is an overlay transparent panel with a list of actions rendered with bitmap font at native screen resolution (to stand out from the book text behind it).
* a minimal cursor shows the current menu choice, defaults to first.
* move through menu with dpad up and down, select with A and escape with B (or by closing the menu with the X button).
* Menu currently contains these actions:
    * back to book list (button)
    * exit app (button)

#### Embedded Music Player

Available when in book reader screen. Music doesn't start by itself or resume from last playback, user must initiate playback everytime they open a new book. Music stops when user goes back to books list screen.

* Plays mp3 files found in predetermined music folder.
* Music tracks folder is scanned only once at start of App.
* Starting and pausing/resuming music is done by pressing the A button briefly (as long as the popup menu is closed)
* Tracks can be skipped by holding A button (one skip per button hold)
* Music plays in shuffle mode, a random playlist is generated everytime playback is started and if playlist is finished, a new one is generated and played. Playlist is a shuffled list of all tracks found.
* Volume is controlled with the physical volume buttons, defaults to 50%, and remembers its last setting.
