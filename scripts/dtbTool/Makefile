OBJ_FILES := dtbtool.o
CFLAGS := -static -I$(LIBFDT_DIR) -O2 -fomit-frame-pointer -Wall

all: dtbTool

dtbTool: $(OBJ_FILES)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJ_FILES)

