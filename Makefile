compile: main.cpp
	g++ main.cpp -o main.o -g -std=c++14

cont: main.cpp
	g++ main.cpp -o main.o -g -std=c++14
	./main.o cont 3
sw1: main.cpp
	g++ main.cpp -o main.o -g -std=c++14
	./main.o sw1 src.txt null sw2 100-999
sw2: main.cpp
	g++ main.cpp -o main.o -g -std=c++14

sw3: main.cpp
	g++ main.cpp -o main.o -g -std=c++14

tar: clean
	cd .. && tar -czf submit.tar.gz submit
clean:
	rm -rf *.o
	rm -rf fifo*
