######################################################################
##  see also github.com/Leporacanthicus/lacsap/blob/master/Makefile
######################################################################
SOURCES = *.cpp
HEADERS = *.hpp
OBJ = ${SOURCES:.cpp=.o}

#CC = llvm-g++
CC = g++

#LLVM_DIR = /usr/local
LLVM_DIR = /usr/local/Cellar/llvm@4/4.0.1_1

#CFLAGS = -g -O3 -I llvm/include -I llvm/build/include -I ./
CFLAGS = -std=c++11

#LLVMFLAGS = `/usr/local/bin/llvm-config --cxxflags --ldflags --system-libs --libs all`

CXXFLAGS = `$(LLVM_DIR)/bin/llvm-config --cxxflags` -fno-rtti
LDFLAGS = `$(LLVM_DIR)/bin/llvm-config --ldflags`
LLVMLIBS = `$(LLVM_DIR)/bin/llvm-config --system-libs --libs all`

.PHONY: ch2 ch3

all: ch2 ch3

ch2: toy_ch2.o
#${CC} ${CFLAGS} ${LLVMFLAGS} $< -o $@
	${CC} ${LDLAGS} ${LLVMFLAGS} $< ${LLVMLIBS} -o $@

ch3: toy_ch3.o
#${CC} ${CFLAGS} ${LLVMFLAGS} $< -o $@
	${CC} ${LDLAGS} ${LLVMFLAGS} $< ${LLVMLIBS} -o $@

%.o: %.cpp ${HEADERS}
	${CC} ${CFLAGS} ${CXXFLAGS} -c $< -o $@

clean:
	rm -f -r a.out ch2 ch3 ${OBJ}

