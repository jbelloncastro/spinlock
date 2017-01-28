CXXFLAGS=-std=c++11 -pthread -O3

all: rwlock_test

clean:
	rm -f rwlock_test
