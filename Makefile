# Define the compiler
CC=g++

# Define any compile-time flags
CFLAGS=-std=c++11 -Wall -lpthread

# Define the source files
SOURCES=simulator.cpp WriteOutput.c helper.c

# Define the output file
OUTPUT=simulator

# Default target
all: $(OUTPUT)

$(OUTPUT): $(SOURCES)
	$(CC) -o $@ $(SOURCES) $(CFLAGS)

# Clean target to remove executable
clean:
	rm -f $(OUTPUT)
