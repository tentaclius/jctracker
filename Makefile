BIN  = jctracker
LIBS = -ljack -lpthread -lm
OPTS = -Wall -g -DDEBUG

$(BIN): tracker.cpp Makefile
	$(CXX) $(OPTS) tracker.cpp -o $@ $(LIBS)

clear:
	rm -f jctracker

re: clear jctracker
