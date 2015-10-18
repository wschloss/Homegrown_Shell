# Homegrown Shell
* Walter Schlosser *

## Description:
Traditional bash-like shell made in C++ for *nix systems.  See the builtins.h
file for supported built in commands.  The shell also supports:
* External Commands
* Piping ( com | com | com )
* File redirection ( com > file OR com < file OR com >> file )
* Tab completion using programs in you $PATH
* Backgrounding ( com & ) (This feature is buggy at the moment)

## Known Bugs:
Backgrounding is buggy.  Process successfully executes in background, but then
the main shell interface may print out of order.  Additional buggyness ensues.

## Build instructions:
This sheel depends on the GNU readline library.
* A makefile is provided, run 'make' next to shell.cpp 
* The executable is named 'myshell'
