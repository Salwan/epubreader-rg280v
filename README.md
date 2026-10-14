EPUB Book Reader for RG280V
=========================================

Would be awesome if I can read epub books on my RG280V.

It has a strange chipset, but there are already many apps made for it using the platform name "OpenDingux" specifically for an older Anbernic device RG350.

## Resources for development

* [OpenDingux Linux code repositories](https://github.com/OpenDingux)
* [List of software packages for OpenDingux with source code for many](https://github.com/retrogamehandheld/OpenDingux)
* [Apps for RG350](https://github.com/RafaVico/RG350_APPREPOSITORY)

These apps are useful for reference and they work well on the RG280V:

* [Commander file manager](https://github.com/glebm/rs97-commander), uses a nice native-resolution font
* [GMU Music Player](https://github.com/denis-n-kuznetsov/gmu), supports mp3 playback
* PdfViewer, source downloaded in: `~/dev/opendingux.pdfviewer/pdfviewer.sourcecode.Hardyx/`

## App Design

App drawing should look perfect on the weird RG280V screen: 320x480, but it's a 4:3 aspect ratio screen which means actual square pixels are made of 2 vertical pixels making ratio correct resolution: 320x240

Starting the app by default views the Books list screen. If a book was opened last time, it will open the same book at the same place.

Default theme is dark. Default font size is medium. Both are remembered when changed.

At any point during the app, pressing select + start exits.

### Books List screen

Uses a bitmap native resolution text (small and sharp) to display the list of books.

Lists all the books found in the folder with the status of each book (percentage of completion) and a checkmark if the book has been read.

Shows current active theme from available themes (remembers which one was set last): dark (default), warm, amber, matrix, and light.

Themes can be changed by pressing R2 and L2 buttons.

Shows current active book font size (remembers last set): small, medium (default), large.

Book rendering font size can be changed by toggled through presets by pressing Y button. This is the only place font size is changeable (in the books list) because they'll change how many pages books have.

Books are sorted alphabetically. Finished books are listed at the end. There's a progress bar to the right (a 2 pixel wide bar) for when the list overflows the screen.

* A cursor that highlights the current book is used to select which book to open.
* dpad up and down to move up and down.
* dpad right and left to move entire pages (page up and down basically) or if it's a small list, just jumps to first and last page respectively.
* moving between selections plays a very brief warm chirp for better feel.
* holding dpad up or down behaves like expected: moves one book up/down, then after a short delay key repeat kicks in and it quickly moves through books.
* book can be opened by pressing A button.

### Book Reader screen

The reading experience is focused with only these two components shown during reading:

* the actual book text rendered using truetype utf-8 font at the preset size
* a 1-pixel high bottom bar indicating the current reading position, color used here should be low contrast so they don't stand out.

in-book hyperlinks are rendered but aren't clickable/interactable (minimalism is the point).

Theme can be changed by pressing R2 and L2 buttons, but font-size is unchangable from the book reading screen.

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
* Volume is controlled with the physical volume buttons, defaults to 50% and remembers were it was last.
