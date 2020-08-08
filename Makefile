LIBS = -lSDL2
FLAGS = -std=c99 -pedantic -Wpedantic -Wall -Werror -O3
# comment this line if you are under Linux
LIBS-WIN32 = -lmingw32 -lSDL2main -Wl,-subsystem,windows
CC = gcc

reinetteII+: reinetteII+.c puce6502.c
	$(CC) $^ $(FLAGS) $(LIBS-WIN32) $(LIBS) -o $@

all: reinetteII+
