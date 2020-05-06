CXX=`llvm-config --bindir`/clang
CXXFLAGS=`llvm-config --cxxflags` -shared -fPIC
LDFLAGS=`llvm-config --ldflags`

all: MyPass.so

MyPass.so: SimpleDataDependenceGraph.cpp SimpleDataDependenceGraph.h MyPass.cpp MyPass.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ SimpleDataDependenceGraph.cpp MyPass.cpp 
