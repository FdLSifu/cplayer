CXXFLAGS=-O3
all: cplayer.o
	g++ $(CXXFLAGS)  -c -o cplayer.o cplayer.cpp
	g++ $(CXXFLAGS) -o cplayer $< -lcurl 
	rm cplayer.o
clean:
	rm cplayer
