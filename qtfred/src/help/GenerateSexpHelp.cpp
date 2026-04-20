#include <cstdio>
#include <cstdlib>
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

static bool inject_generated_keywords(const char* qhp_path, const char* keyword_fragment_path) {
	std::ifstream in(qhp_path, std::ios::binary);
	if (!in.is_open()) {
		return false;
	}

	std::string qhp_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	std::ifstream frag_in(keyword_fragment_path, std::ios::binary);
	if (!frag_in.is_open()) {
		return false;
	}
	std::string keyword_fragment((std::istreambuf_iterator<char>(frag_in)), std::istreambuf_iterator<char>());
	frag_in.close();

	const auto insert_pos = qhp_text.find("</keywords>");
	if (insert_pos == std::string::npos) {
		return false;
	}
	qhp_text.insert(insert_pos, "\n            <!-- BEGIN GENERATED SEXP KEYWORDS -->\n" + keyword_fragment +
	                            "            <!-- END GENERATED SEXP KEYWORDS -->\n");

	std::ofstream out(qhp_path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out << qhp_text;
	return out.good();
}

int main(int argc, char** argv) {
	if (argc != 4) {
		std::fprintf(stderr, "Usage: %s <output-html-path> <keywords-fragment-path> <staged-qhp-path>\n",
		             argc > 0 ? argv[0] : "qtfred_sexp_help_gen");
		return EXIT_FAILURE;
	}

	const char* output_html_path        = argv[1];
	const char* output_keywords_path    = argv[2];
	const char* staged_qhp_path         = argv[3];
	if (!output_sexps_qtfred_help(output_html_path, output_keywords_path)) {
		std::fprintf(stderr, "Failed to generate QtFRED SEXP help output at: %s\n", output_html_path);
		return EXIT_FAILURE;
	}
	if (!inject_generated_keywords(staged_qhp_path, output_keywords_path)) {
		std::fprintf(stderr, "Failed to inject generated SEXP keywords into staged qhp: %s\n", staged_qhp_path);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
