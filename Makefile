COMPILER = gcc
FILESYSTEM_FILES = mailFS.c mail.c cdecode.c

build: $(FILESYSTEM_FILES)
	$(COMPILER) $(FILESYSTEM_FILES) -lcurl -o mailFS `pkg-config fuse --cflags --libs`

clean:
	rm mailFS
