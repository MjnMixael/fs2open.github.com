#include <cstdio>
#include <cstdlib>

#include "parse/sexp.h"

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "Usage: %s <output-html-path>\n", argc > 0 ? argv[0] : "qtfred_sexp_help_gen");
		return EXIT_FAILURE;
	}

	const char* outputPath = argv[1];
	if (!output_sexps(outputPath)) {
		std::fprintf(stderr, "Failed to generate SEXP help at: %s\n", outputPath);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
