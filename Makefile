BIN  = jctracker
LIBS = -ljack -lpthread -lm
OPTS_DEV = -Wall -std=c++11 -g -DDEBUG
OPTS =     -Wall -std=c++11

$(BIN): tracker.cpp Makefile
	$(CXX) tracker.cpp -o $@ $(LIBS) $(OPTS) 

devel: tracker.cpp Makefile
	$(CXX) tracker.cpp -o $(BIN) $(LIBS) $(OPTS_DEV) 

prod: tracker.cpp Makefile
	$(CXX) tracker.cpp -o $(BIN).x86_64 $(LIBS) $(OPTS_DEV)

clear:
	rm -f jctracker

re: clear devel
