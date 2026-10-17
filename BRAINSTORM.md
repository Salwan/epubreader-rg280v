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

## Runtime Issues

* **RESOLVED AND ACCEPTED — 0.5.8** (Book viewing issue; original report): two of the books I tested show the first title page(s) correctly but then when moving to the next pages (with dpad-right or R1 button), all I get is an empty screen no matter how many pages I move through. I also noticed that the info popup shows the page stuck on PAGE 2 or PAGE 3 (depending on which page has the book title apparently?). The only way I was able to get to the book's content is to use the jump slider and jump forward by a percentage which jumps into that part of the book correctly (page renders) and then I have to move back through the pages manually until reaching the first page of the book's content. Included one of the books that the problem occurs: `tmp/stuck-on-page3.epub`
    * Fixed by preserving the exact displayed page during neighbor prefetch. The user verified 0.5.8 on the physical RG280V and accepted the fix on 2026-10-17. See [verification and device acceptance](docs/VERIFICATION-0.5.8.md#device-acceptance).

## Future Work

Ideas for the future after epubreader is complete:

* add support for azw3 (KF8) format as the second ebook format (there's already opensource projects that implement support for it)
* add support for mobi which is an older amazon format
* implement a method to allow interacting with hyperlinks in the book that lead to a page/section within the book itself (not external links), adding a mouse cursor mode solely for clicking a hyperlink the way to go, perhaps can be activated by holding a button


## Feature Requests

From testing the epubreader app on device, I'll develop feature request concepts here.

### Battery Percentage info (IMPLEMENTED)

Battery percentage is a critical piece of info since the RG280V device has no warnings or method to indicate low battery.

* battery percentage should be part of the info shown when the popup menu is open
* battery charging status is tracked
* a warning visual should be shown when battery drops below 15% (and battery is not in charging state), indicator remains on screen in the form of a static red frame around the book page, maybe 2 pixels wide to make it more visible.
* When reaching 5% battery (and battery is not in charging state), the book should be closed automatically and app transitions back to book list (to store the book current state before battery dies), then show a popup notification (in book list screen) that reading was interrupted because battery reached 5% in order not to lose progress. User is free to continue reading by reselecting the book until battery dies anyway. This procedure should resolve the situation when user falls asleep while reading a book, in which case the app will exit the book and store the status at 5% battery, then the device battery dies while on the books list screen which should be safer.

### Low-battery shutdown (IMPLEMENTED)

I can shutdown the RG280V from the SimpleMenu interface and pretty quickly. Is there a way to initiate a shutdown from within the epubreader app?

If there's, this is an additional useful action if the user fell asleep during reading. Auto-shutdown should be initiated when battery reaches 2% (as long as device is not currently charging), this should happen app-wide (regardless of which screen app is on).

### Music Repeat (IMPLEMENTED)

Add a new menu item in the popup menu with 3 repeat options: `ALL` (default), `ONE`, and `STOP`

* `ALL`: current music playback approach, plays tracks from a shuffled playlist endlessly
* `ONE`: plays only the current track and repeats endlessly
* `STOP`: once playback of current track ends, automatically stop music playback

In all modes, user can move to the next track by holding `A` as normal, so repeat mode only determines what happens when playback of current track ends.

### UI Tweaks (IMPLEMENTED)

* In Books List screen, list of books should be grouped by: books that are more than 0% and less than 100% come first (reading), books that are 0% come second (unread), books that are 100% come last (read).

### Better File System Integration (IMPLEMENTED)

This applies to Adam 2.1 specifically which is the last firmware version released for the RG280V.

On the RG280V, the roms, apps, and user files are all on a second dedicated sdcard mounted to `/media/sdcard`, while the first sdcard (with the home folder) is dedicated to the firmware and OS specifically. This design is used so that in case the OS is corrupted in some way or needs to be upgraded or replaced, then no user files will be lost and after a fresh firmware/OS flash everything is where it was left.

At the moment, epubreader is exclusively using the OS sdcard for user files storage which may not be a good idea since a reflash means it'll loss all user data.

I'm already placing the actual app executable in `/media/sdcard/apps`, so this change offers more consistency.

This feature request is for changing epubreader's paths:

* Change `~/.epubreader` config/state/cache path to instead go in: `/media/sdcard/epubreader`
* Change default `~/books` and `~/music` folders to instead use: `/media/sdcard/books` and `/media/sdcard/music`
