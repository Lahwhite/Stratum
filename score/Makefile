CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g
LDFLAGS  := -lreadline
TARGET   := score
SRCS     := $(shell find src -name '*.cpp')
OBJS     := $(SRCS:.cpp=.o)
INC      := -Iinclude

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INC) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)