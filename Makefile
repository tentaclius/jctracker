BIN  = jctracker
LIBS = -ljack -lpthread -lm
OPTS_DEV = -Wall -std=c++11 -g -DDEBUG
OPTS_PROD = -Wall -std=c++11

OBJECTS = common.o events.o jackengine.o midictlevent.o midiheap.o midimessage.o noteevent.o parser.o sequencer.o
COMMON_DEPS = Makefile common.h

$(BIN): main.cpp $(COMMON_DEPS) $(OBJECTS)
	$(CXX) main.cpp -o $@ $(OBJECTS) $(LIBS) $(OPTS_DEV) 

%.o: %.cpp %.h $(COMMON_DEPS)
	$(CXX) -c $< $(OPTS_DEV)

clear:
	rm -f jctracker
	rm -f *.o

clean:
	rm -f *.o

re: clear $(BIN)

prod: tracker.cpp Makefile
	$(CXX) tracker.cpp -o $(BIN).x86_64 $(LIBS) $(OPTS_PROD)
