#include "precompiled.hpp"
#include "puzzle.hpp"

int main(int argc, char* argv[]) {
	std::unique_ptr<Puzzle> puzzle = Puzzle::fromNONFile(argv[1]);
	std::cout << puzzle->name() << std::endl;
	return 0;
}