all: cplayer.o
	g++ -o cplayer $< -lcurl -O3
	rm cplayer.o
clean:
	rm cplayer
