CPP	:= g++ -Wall -g -fPIC
INC	:= -I/usr/include -I ../
DEFS	:= 
SRCS_C	:= $(wildcard *.c)
SRCS_CPP:= $(wildcard *.cpp)
OBJS	:= $(subst .c,.o,$(SRCS_C)) $(subst .cpp,.o,$(SRC_CPP))
 
all: $(OBJS)

%.o: %.c
	$(CPP) -o $@ $(INC) $(DEFS) -c $<

%.o: %.cpp
	$(CPP) -o $@ $(INC) $(DEFS) -c $<

clean:
	/bin/rm -f *.o
