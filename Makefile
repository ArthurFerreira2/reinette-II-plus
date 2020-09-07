CC = gcc
FLAGS = -std=c99 -pedantic -Wpedantic -Wall -O3

LIBS = -lSDL2
# comment these two lines if you are under Linux :
WIN32-LIBS = -lmingw32 -lSDL2main -Wl,-subsystem,windows
WIN32-RES = reinetteII+.res

reinetteII+: reinetteII+.c puce6502.c $(WIN32-RES)
	$(CC) $^ $(FLAGS) $(WIN32-LIBS) $(LIBS) -o $@

reinetteII+.res: reinetteII+.rc
	windres $^ -O coff -o $(WIN32-RES)

all: reinetteII+
