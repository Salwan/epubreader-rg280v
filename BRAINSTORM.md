# EPUB Reader for RG280V

## Requirements

### Brainstorm

* single '.opk' executable for OpenDingux (Ingenic 4770-based chipset)
* native RG280V: 320x480 resolution
* looks for epub files in a predetermined folder
* starts in book list menu: a list of all the epub files in the folder and an indicator for how far the user is in each book. Finished books are listed at the end with a checkmark/finished indicator.
* renders epub text using true-type font with utf-8 encoding
* 3 sizes of text: small, medium, and large
* font size is shown read-only in the book list and changed inside the reader popup menu, preserving the text anchor when the book re-paginates
* remembers where each book was the last time it was opened
* when closing the app while reading a book, next time app is opened, the same book is opened at the same place
* shows a small low profile progress bar at the bottom indicating where in the book the user is reading (a 1 pixel high bar is enough)
* direct integration with RG280V's controls: dpad, standard 4 face buttons, select and start, L1, R1, L2, R2, and volume up and down
* can move between pages with dpad right and left, and also using R1 and L1
* provides slider to quickly jump to any section of the book, slider can be opened/closed with select button
* can exit the app at any time by hitting: select + start
* popup menu can be opened/closed with the X button, inside menu button A for selection and button B for escape. When popup menu is shown, also render a panel at the top with the book info (title, file, page count, etc). The menu provides these actions for now:
    * back to book list (button)
    * font size (small / medium / large): highlight the row with dpad up/down, then adjust with left/right; clamp at the ends and keep the menu open
    * exit app (button)
* 3 themes via simple yaml definitions: dark, warm, amber, matrix, and light
* themes can be switched between with R2 and L2 buttons

### Embedded Background Music Player

* Embedded music player for mp3 files.
* App looks for mp3 files in a predetermined folder.
* Music is not playing by default.
* Starting and pausing/resuming music is done by pressing the A button briefly
* Tracks can be skipped by holding A button (one skip per button hold)
* Music plays in shuffle mode, a random playlist is generated everytime playback is started and if playlist is finished, a new one is generated and played.
* Volume is controlled with the physical volume buttons, defaults to 50% and remembers were it was last.

## Future Work

Ideas for the future after epubreader is complete:

* add support for azw3 (KF8) format as the second ebook format (there's already opensource projects that implement support for it)
* implement a method to allow interacting with hyperlinks in the book that lead to a page/section within the book itself (not external links), adding a mouse cursor mode solely for clicking a hyperlink the way to go, perhaps can be activated by holding a button
