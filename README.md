# NI - A minimalist modal text editor

NI is a minimalist modal text editor that aims to provide basic file editing
functionality in under 1000 lines of C code. Based on the [Antirez' Kilo
editor](http://antirez.com/news/108). The idea is to make hard choices about
which features provide the most value given the constraints. This should
hopefully result in an increase in the average value per line over time. NI is
not a code golfing exercise. The code strives to be readable and maintainable.

## TODO

- welcome screen listing keymaps
- delete + motion
- command line prompt
- searching
- incremental search
- debug layer
- saveas
- static memory allocation
- key chords
- undo / redo
- syntax highlighting (?)
- setting options (?)
- suspend & resume (?)
- multiple buffers & load file (?)

## NOTES

**Manage memory of the line being edited?**

Load line into a WIP buffer and commit when leaving insert mode?
How would we deal with multi-line editing?
