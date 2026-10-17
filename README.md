EPUB Book Reader for RG280V
=========================================

Specific for Adam 2.1 (OpenDingux-based) firmware on Anbernic RG280V, it won't work for other Adam 2.1 devices that use a different screen as this is optimized to render on the RG280V resolution.

## Install

* Copy the epub reader's `.OPK` file to: `/media/sdcard/apps`
* Copy your epub books to: `/media/sdcard/books`
* Copy your ambient mp3 files to: `/media/sdcard/music`

Try [Project Gutenberg](https://www.gutenberg.org/) for a near infinite number of epub books.

## Features

* Native App, shows up in Apps menu as `EPUB Reader`
* Lists books in 3 groups: reading, unread, and read
* Remembers app state: if you were reading a book when you closed app, it resumes from there
* Can exit app at any time with the standard SELECT+START hotkey, saves state prior to exiting
* Selection of nice dark and light themes, switch between them with L2 and R2
* Three font sizes optimized for the RG280V screen: SMALL, MEDIUM, and LARGE
* Turn pages with: DPAD LEFT/RIGHT, and L1/R1
* Can jump to any part of the book with a slider: SELECT while reading a book
* Monitors battery percentage and shows a visual warning when battery is low, and in case user fell asleep, will safely shutdown device when reaching 2%
* Embedded mp3 player let's you play your ambient music tracks, download your favorite 1 hour of rain/ocean waves/fireplace/etc as mp3 and place it in the music folder
* Embedded images are correctly rendered

## Controls

All controls are shown in App, so no need to memorize anything.

App hotkeys:

* SELECT+START: closes app
* R2 and L2: switch to next/previous theme

In books List:

* DPAD to navigate through books
* A to begin reading a book
* X shows the settings popup which allows setting a different books/music folder and to initiate a rescan of the library

During book reading:

* DPAD Right/Left, R1/L1: to flip between pages
* X to show popup menu which lets you close book, change font size and music repeat mode
* Press A to start/stop ambient music playback
* Hold A while music is playing to jump to next track
* SELECT to show the jump slider which allows jumping to any place in the book
* Volume buttons to raise/lower music volume

## Limitations

* English prose only, though fonts used are unicode and text renders as utf8 so many languages that use latin-like alphabets should display perfectly, maybe even more complex languages but there are no guarantees
* no mathematical notation (untested), or tables (buggy due to screen resolution limits)
* no in-book hyperlinks support
* epub for books, mp3 for ambient tracks
* tested and verified on Anbernic RG280V with Adam 2.1 firmware only

## Wishlist

* verified support for non-English languages including complex and right-to-left ones
* virtual mouse cursor that can click on in-book hyperlinks

## AI Usage Disclaimer

While this app was designed by me (down to individual pixels in some cases) and every feature/milestone was verified directly on the physical device. AI wrote the docs, build files, all of the code, and test suites.
