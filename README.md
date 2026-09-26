# gg-editor

[![NO AI](https://raw.githubusercontent.com/nuxy/no-ai-badge/master/badge.svg)](https://github.com/nuxy/no-ai-badge)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
![PRs](https://img.shields.io/badge/PRs-welcome-brightgreen)

This is a screen editor for Posix complaint operating systems (Linux, *BSD, Solaris, etc).
It's a spare time project, and isn't ready to be used by anyone but myself. I'll be updating
this README with additional details as the project progresses. 

## Keybindings

| Key | Action |
| --- | --- |
| Arrow keys | Move the cursor |
| `ctrl-a` or `home` | Move to the beginning of the current line |
| `ctrl-e` or `end` | Move to the end of the current line |
| `ctrl-b` or `pgup` | Move up by one screen of text |
| `ctrl-v` or `pgdn` | Move down by one screen of text |
| `ctrl-f` | Search for text and move to the next match |
| `ctrl-s` | Save the current file |
| `ctrl-q` | Quit; press it again to confirm when there are unsaved changes |

In Apple's Terminal on macOS, hold Shift when pressing Page Up or Page Down
to send those keys to the editor. Alternatively, use `ctrl-b` and `ctrl-v`.
The Control-key bindings work on both Linux and macOS; use Control, not Command.
