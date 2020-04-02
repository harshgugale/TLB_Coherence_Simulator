SRC_DIR := $(PWD)
OBJ_DIR := $(PWD)/obj
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))
CXXFLAGS := -std=c++11 -fPIC -I . -O3

main.exe: $(OBJ_FILES)
	g++ -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	g++ $(CXXFLAGS) -c -o $@ $<

clean:
	rm $(OBJ_DIR)/*.o

#g++ -std=c++11 -O3 *.cpp -I .. -DNUM_TRACES_PER_CORE=2000000000 -DWARMUP=1000000000 -DBENCHMARK=1 -o httpd_cotag -DCOTAG
#echo "Compiling httpd cotagless"
