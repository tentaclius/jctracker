BIN  = jctracker
LIBS = -ljack -lpthread -lm
OPTS_DEV = -Wall -std=c++11 -g -DDEBUG
OPTS =     -Wall -std=c++11

$(BIN): tracker.cpp Makefile
	$(CXX) tracker.cpp -o $@ $(LIBS) $(OPTS_DEV) 

clear:
	rm -f jctracker

re: clear $(BIN)
