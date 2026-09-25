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
| Printable characters | Insert text |
| Enter | Insert a new line |
| Backspace or `Ctrl-H` | Delete the character before the cursor; at column 0, join with the previous line |
| `Ctrl-S` | Save the current file |
| `Ctrl-Q` | Quit; press it again to confirm when there are unsaved changes |
