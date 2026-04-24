# hello
### https://www.cs.swarthmore.edu/~newhall/unixhelp/howto_makefiles.html ###

CC := g++
CFLAGS := -Wall -Wextra -g
MAIN := acne

VPATH := src:src/*.cpp

OBJ_DIR := obj
_OBJS := main.o utah.o
OBJS := $(patsubst %, $(OBJ_DIR)/%, $(_OBJS) )

.PHONY:	depend clean

all: $(MAIN)
	@echo \`$(MAIN)\' compiled

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(MAIN)

$(OBJ_DIR)/%.o : %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@


clean:
	$(RM) $(OBJ_DIR)/*.o *~

