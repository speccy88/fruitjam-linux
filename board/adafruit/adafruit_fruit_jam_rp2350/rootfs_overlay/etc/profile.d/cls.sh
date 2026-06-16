# BusyBox clear applet is not enabled in this image; provide ANSI equivalents.
clear() { printf "\033[2J\033[H"; }
cls() { clear; }
