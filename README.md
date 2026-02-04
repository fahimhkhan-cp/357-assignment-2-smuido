[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/nymGEqBZ)
[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/HXGUViES)
[![Open in Visual Studio Code](https://classroom.github.com/assets/open-in-vscode-2e0aaae1b6195c2367325f4f02e2d04e9abb55f0b24a779b69b11b9e10269abc.svg)](https://classroom.github.com/online_ide?assignment_repo_id=22286683&assignment_repo_type=AssignmentRepo)
# 357-assignment-2

<h2>File System Emulator</h2>

This program emulates a simplified UNIX-like file system using regular files and directories. It supports directories, regular files, and basic navigation using inodes stored in a fixed-size table.

<h3>Build</h3></h3>

Compile using C99:

<code>gcc -std=c99 -Wall -Wextra -g fs_emulator.c -o fs_emulator</code>

<h3>Run</h3>

Run the emulator by providing the path to a directory that contains the emulated file system:

<code>./fs_emulator <fs_directory></code>

Example:

<code>./fs_emulator myfs</code>

The program starts in inode 0 (the root directory).

<h3>Supported Commands</h3>

<code>ls</code> — list contents of the current directory

<code>cd <name></code> — change into a subdirectory

<code>mkdir <name></code> — create a new directory

<code>touch <name></code> — create a new regular file

<code>exit</code> or Ctrl-D — save state and exit

Paths are not supported; all operations are relative to the current directory.

State is persisted via the inodes_list file on exit.

**The code in this assignment is done by myself with no use of AI.**
