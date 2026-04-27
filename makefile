# hello
### https://www.cs.swarthmore.edu/~newhall/unixhelp/howto_makefiles.html ###

CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -g
MAIN := testing/acne

VPATH := src:src/*.cpp

OBJ_DIR := obj
_OBJS := main.o utah.o lexer.o parser.o lower.o linear.o simulate.o debug.o audio.o render.o
OBJS := $(patsubst %, $(OBJ_DIR)/%, $(_OBJS) )

.PHONY:	depend clean

all: $(MAIN)
	@echo \`$(MAIN)\' compiled

$(MAIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(MAIN)

$(OBJ_DIR)/%.o : %.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<  -o $@


clean:
	$(RM) $(OBJ_DIR)/*.o *~
