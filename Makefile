BIN  = jctracker
LIBS = -ljack -lpthread -lm
OPTS_DEV = -Wall -std=c++11 -g -DDEBUG
OPTS_PROD =     -Wall -std=c++11

$(BIN): tracker.cpp Makefile
	$(CXX) tracker.cpp -o $@ $(LIBS) $(OPTS_DEV) 

clear:
	rm -f jctracker

re: clear $(BIN)

prod: tracker.cpp Makefile
	$(CXX) tracker.cpp -o $(BIN).x86_64 $(LIBS) $(OPTS_PROD)
