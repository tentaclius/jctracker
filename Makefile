BIN  = jctracker
LIBS = -ljack -lpthread -lm
OPTS_DEV = -Wall -g -DDEBUG
OPTS = -Wall

$(BIN): tracker.cpp Makefile
	$(CXX) $(OPTS) tracker.cpp -o $@ $(LIBS)

devel: tracker.cpp Makefile
	$(CXX) $(OPTS_DEV) tracker.cpp -o $(BIN) $(LIBS)

prod: tracker.cpp Makefile
	$(CXX) $(OPTS_DEV) tracker.cpp -o $(BIN).x86_64 $(LIBS)

clear:
	rm -f jctracker

re: clear jctracker
