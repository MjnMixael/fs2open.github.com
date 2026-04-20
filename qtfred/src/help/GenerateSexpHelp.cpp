#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#define SDL_MAIN_HANDLED
#endif

#include "parse/sexp.h"

#ifdef main
#undef main
#endif

static void replace_once(std::string& text, const char* from, const char* to) {
	const auto pos = text.find(from);
	if (pos != std::string::npos) {
		text.replace(pos, std::strlen(from), to);
	}
}

static bool postprocess_generated_html(const char* path) {
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return false;
	}

	std::string html((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	replace_once(html, "<title>SEXP Output - FSO v", "<title>SEXP Operator Reference - FSO v");
	replace_once(html, "<h1>SEXP Output - FSO v", "<h1>SEXP Operator Reference - FSO v");

	constexpr const char* kHeaderInsert =
	    "</h1>\n"
	    "<p><strong>Scope:</strong> This page lists built-in (C++) SEXP operators compiled into the engine.</p>\n"
	    "<p><strong>Note:</strong> Lua-defined SEXP operators are created at runtime and are not included in this reference.</p>\n";

	const auto h1ClosePos = html.find("</h1>");
	if (h1ClosePos != std::string::npos) {
		html.insert(h1ClosePos + std::strlen("</h1>"), kHeaderInsert);
	}

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out << html;
	return out.good();
}

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
	if (!postprocess_generated_html(outputPath)) {
		std::fprintf(stderr, "Failed to post-process generated SEXP help at: %s\n", outputPath);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
