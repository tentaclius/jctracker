BIN  = jctracker
LIBS = -ljack -lpthread -lm
OPTS = -Wall -std=c++11 -g -DDEBUG

OBJECTS = common.o events.o jackengine.o midictlevent.o midiheap.o midimessage.o noteevent.o parser.o sequencer.o
COMMON_DEPS = Makefile common.h

$(BIN): main.cpp $(COMMON_DEPS) $(OBJECTS)
	$(CXX) main.cpp -o $@ $(OBJECTS) $(LIBS) $(OPTS) 

%.o: %.cpp %.h $(COMMON_DEPS)
	$(CXX) -c $< $(OPTS)

clear:
	rm -f jctracker
	rm -f *.o

clean:
	rm -f *.o

re: clear $(BIN)
