# NI - A minimalist modal text editor

NI is a minimalist modal text editor that aims to provide basic file editing
functionality in under 1000 lines of C code (ignoring comments and empty lines).
Based on the [Antirez' Kilo editor](http://antirez.com/news/108).

## Keymaps

### Normal Mode

```
  Movement
  --------
  hjkl        left/down/up/right

  Closing / Saving etc.
  ---------------------
  q           quit without saving
  ctrl-q      quit without saving (error exit code)
  ctrl-s      save file
  ZZ          save and exit
  ZQ          exit without saving

  Entering insert mode
  --------------------
  i           enter insert mode
  a           enter insert mode after the cursor
  I           enter insert mode at start of line
  A           enter insert mode at end   of line

  Scrolling the viewport
  ----------------------
  ctrl-y      scroll up
  ctrl-e      scroll down
  ctrl-l      scroll left
  ctrl-h      scroll right

  Jumps
  -----
  gg          jump to first line
  G           jump to last line
  ctrl-d      jump half a page down
  ctrl-u      jump half a page up

  Motions
  -------
  0           move cursor to the start    of line
  $           move cursor to the end      of line
  w           move cursor to the next     beginning of a word
  b           move cursor to the previous beginning of a word
  e           move cursor to the next     end of a word
  ge          move cursor to the previous end of a word

  f[c]        find next     [c] in line
  F[c]        find previous [c] in line
  ;           repeat last 'f' or 'F' in the same     direction
  ,           repeat last 'f' or 'F' in the opposite direction

  Actions
  -------
  x           delete character
  dd          delete line
  d[motion]   delete over motion (only 'w' 'b' 'e' for now)
  c[motion]   change over motion (only 'w' 'b' 'e' for now)
  D           delete till the end of line
  C           delete till the end of line and enter insert mode
  o           insert line below and enter insert mode
  O           insert line above and enter insert mode
  J           join cursor line with line below

  Misc
  ----
  ctrl-g      display buffer stats
```

### Insert Mode

```
  <ESC>       exit insert mode
  ctrl-q      exit insert mode
```

---

## TODO

- delete/change + motion
- searching
- incremental search
- add <count> to keys
- undo / redo / dot repeat
- autoindent
- welcome screen listing keymaps (?)
- command line prompt (?)
- static memory allocation (?)
- debug layer (?)
- syntax highlighting (?)
- setting options (?)
- saveas (?)
- multiple buffers & load file (?)
- line wrapping (?)
- suspend & resume (?)
