TARGETS = multiserver coordinator test_coordinator singleserver

all: $(TARGETS)

%.o: %.cc kv.h
	g++ -g $^ -c -o $@

KV: KV.o
	g++ -g $^ -lpthread -o $@

singleserver: singleserver.o KV.o
	g++ -g $^ -lpthread -o $@

multiserver: multiserver.o KV.o
	g++ -g $^ -lpthread -o $@

coordinator: coordinator.o KV.o
	g++ -g $^ -lpthread -o $@

test_coordinator: test_coordinator.o
	g++ -g $^ -lpthread -o $@

pack:
	rm -f submit-hw3.zip
	zip -r submit-hw3.zip README Makefile *.c* *.h*

clean::
	rm -fv $(TARGETS) *~ *.o submit-hw3.zip

realclean:: clean
	rm -fv submit-hw3.zip

