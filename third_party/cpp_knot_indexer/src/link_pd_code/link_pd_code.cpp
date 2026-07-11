#include "link_pd_code.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string readAll(std::istream& input) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw cki::link_pd_code::ProjectionError("cannot open input file: " + path.string());
    }
    return readAll(input);
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    if (!output) {
        throw cki::link_pd_code::ProjectionError("cannot open output file: " + path.string());
    }
    output << text;
}

void printUsage(std::ostream& output) {
    output
        << "Usage: link_pd_code [OPTIONS] [INPUT]\n"
        << "\n"
        << "Read ordered closed 3D link components and write a PD code.\n"
        << "If INPUT and --input are omitted, input is read from stdin.\n"
        << "\n"
        << "Input format:\n"
        << "  component_count\n"
        << "  point_count_for_component_0\n"
        << "  x y z\n"
        << "  ...\n"
        << "\n"
        << "Options:\n"
        << "  --input, -i PATH              Read coordinates from PATH.\n"
        << "  --output, -o PATH             Write PD code to PATH instead of stdout.\n"
        << "  --max-attempts N              Projection directions to try. Default: 256.\n"
        << "  --epsilon VALUE               Numeric tolerance. Default: 1e-10.\n"
        << "  --direction X Y Z             Use one explicit projection direction.\n"
        << "  --first-generic               Stop after the first successful generic projection.\n"
        << "  --prefer-min-crossings        Search all attempts and keep fewer crossings. Default.\n"
        << "  --encode-isolated-components  Encode no-crossing components as degenerate PD crossings.\n"
        << "  --help, -h                    Show this help text.\n";
}

std::string requireValue(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        throw cki::link_pd_code::ProjectionError(option + " requires a value");
    }
    ++index;
    return argv[index];
}

int parseInt(const std::string& text, const std::string& option) {
    std::size_t used = 0;
    int value = 0;
    try {
        value = std::stoi(text, &used, 10);
    } catch (const std::exception&) {
        throw cki::link_pd_code::ProjectionError(option + " requires an integer value");
    }
    if (used != text.size()) {
        throw cki::link_pd_code::ProjectionError(option + " requires an integer value");
    }
    return value;
}

double parseDouble(const std::string& text, const std::string& option) {
    std::size_t used = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &used);
    } catch (const std::exception&) {
        throw cki::link_pd_code::ProjectionError(option + " requires a numeric value");
    }
    if (used != text.size()) {
        throw cki::link_pd_code::ProjectionError(option + " requires a numeric value");
    }
    return value;
}

cki::link_pd_code::Point3 parseDirection(int& index, int argc, char** argv, const std::string& option) {
    if (index + 3 >= argc) {
        throw cki::link_pd_code::ProjectionError(option + " requires X Y Z values");
    }
    const double x = parseDouble(argv[++index], option);
    const double y = parseDouble(argv[++index], option);
    const double z = parseDouble(argv[++index], option);
    return cki::link_pd_code::Point3{x, y, z};
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    bool have_input = false;
    bool have_output = false;
    cki::link_pd_code::Options options;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                printUsage(std::cout);
                return 0;
            }
            if (arg == "--input" || arg == "-i") {
                input_path = requireValue(i, argc, argv, arg);
                have_input = true;
                continue;
            }
            if (arg == "--output" || arg == "-o") {
                output_path = requireValue(i, argc, argv, arg);
                have_output = true;
                continue;
            }
            if (arg == "--max-attempts") {
                options.max_attempts = parseInt(requireValue(i, argc, argv, arg), arg);
                continue;
            }
            if (arg == "--epsilon") {
                options.epsilon = parseDouble(requireValue(i, argc, argv, arg), arg);
                continue;
            }
            if (arg == "--direction") {
                options.direction = parseDirection(i, argc, argv, arg);
                continue;
            }
            if (arg == "--first-generic") {
                options.prefer_min_crossings = false;
                continue;
            }
            if (arg == "--prefer-min-crossings") {
                options.prefer_min_crossings = true;
                continue;
            }
            if (arg == "--encode-isolated-components") {
                options.encode_isolated_components = true;
                continue;
            }
            if (!arg.empty() && arg[0] == '-') {
                throw cki::link_pd_code::ProjectionError("unknown option: " + arg);
            }
            if (have_input) {
                throw cki::link_pd_code::ProjectionError("multiple input paths were provided");
            }
            input_path = arg;
            have_input = true;
        }

        const std::string text = have_input ? readTextFile(input_path) : readAll(std::cin);
        const cki::link_pd_code::Link link = cki::link_pd_code::parseLinkCoordinateText(text);
        const cki::link_pd_code::PDCode pd_code = cki::link_pd_code::computePDCode(link, options);
        const std::string rendered = cki::link_pd_code::formatPDCode(pd_code) + "\n";

        if (have_output) {
            writeTextFile(output_path, rendered);
        } else {
            std::cout << rendered;
        }
        return 0;
    } catch (const cki::link_pd_code::ProjectionError& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
    }
    return 1;
}
