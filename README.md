Mail Filesystem(MailFS)
=======================================

Simple mail filesystem.

Build Instructions:
	1. Install libfuse(2.9+) and libcurl(7.82.0-2+).
	2. Clone repo.
	3. User 'make' to install.

Usage: ./mailFS [options] <mountpoint>

Options: 
	-f: Run in foreground
	-d: Debug
	-s: Single-threaded
	-h: Show help
