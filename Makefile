compile: main.cpp
	g++ main.cpp -o main.o -g -std=c++14

cont: main.cpp
	rm main.o
	g++ main.cpp -o main.o -g -std=c++14
	./main.o cont 3
sw1: main.cpp
	g++ main.cpp -o main.o -g -std=c++14
	./main.o sw1  t3.dat  null sw2  100-110
sw2: main.cpp
	g++ main.cpp -o main.o -g -std=c++14
	./main.o sw2  t3.dat  sw1  sw3  500-550
sw3: main.cpp
	g++ main.cpp -o main.o -g -std=c++14
	./main.o sw3  t3.dat  sw2  null 200-220

tar: clean
	cd .. && tar -czf submit.tar.gz submit
clean:
	rm -rf *.o
	rm -rf fifo*
