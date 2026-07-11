#include "che_to_coord.hpp"
#include "path_utils.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readAll(std::istream& input) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw cki::che_to_coord::ParseError("cannot open input file: " + cki::platform::displayPath(path));
    }
    return readAll(input);
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    if (!output) {
        throw cki::che_to_coord::ParseError("cannot open output file: " + cki::platform::displayPath(path));
    }
    output << text;
}

void printUsage(std::ostream& output) {
    output
        << "Usage: che_to_coord [OPTIONS] [INPUT]\n"
        << "\n"
        << "Read a molecule data file with Atoms and Bonds sections and write ordered 3D link components.\n"
        << "If INPUT and --input are omitted, input is read from stdin.\n"
        << "\n"
        << "Options:\n"
        << "  --input, -i PATH        Read molecule data from PATH.\n"
        << "  --output, -o PATH       Write output to PATH instead of stdout.\n"
        << "  --format FORMAT         Output format: link or atom-id. Default: link.\n"
        << "  --no-prefer-atom-one    Start from the lowest atom id instead of atom 1.\n"
        << "  --help, -h              Show this help text.\n";
}

const cki::platform::ProgramArg& requireValue(
    int& index,
    const std::vector<cki::platform::ProgramArg>& args,
    const std::string& option) {
    if (index + 1 >= static_cast<int>(args.size())) {
        throw cki::che_to_coord::ParseError(option + " requires a value");
    }
    ++index;
    return args[static_cast<std::size_t>(index)];
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<cki::platform::ProgramArg> args = cki::platform::programArguments(argc, argv);
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    bool have_input = false;
    bool have_output = false;
    std::string format = "link";
    cki::che_to_coord::ParseOptions options;

    try {
        for (int i = 1; i < static_cast<int>(args.size()); ++i) {
            const std::string arg = args[static_cast<std::size_t>(i)].text;
            if (arg == "--help" || arg == "-h") {
                printUsage(std::cout);
                return 0;
            }
            if (arg == "--input" || arg == "-i") {
                input_path = requireValue(i, args, arg).path;
                have_input = true;
                continue;
            }
            if (arg == "--output" || arg == "-o") {
                output_path = requireValue(i, args, arg).path;
                have_output = true;
                continue;
            }
            if (arg == "--format") {
                format = requireValue(i, args, arg).text;
                if (format != "link" && format != "atom-id") {
                    throw cki::che_to_coord::ParseError("--format must be 'link' or 'atom-id'");
                }
                continue;
            }
            if (arg == "--no-prefer-atom-one") {
                options.prefer_atom_one_start = false;
                continue;
            }
            if (!arg.empty() && arg[0] == '-') {
                throw cki::che_to_coord::ParseError("unknown option: " + arg);
            }
            if (have_input) {
                throw cki::che_to_coord::ParseError("multiple input paths were provided");
            }
            input_path = args[static_cast<std::size_t>(i)].path;
            have_input = true;
        }

        const std::string text = have_input ? readTextFile(input_path) : readAll(std::cin);
        const cki::che_to_coord::CoordinateLink link =
            cki::che_to_coord::parseCoordinateLinkText(text, options);
        const std::string rendered =
            format == "link" ? cki::che_to_coord::formatLinkCoordinateText(link)
                             : cki::che_to_coord::formatCoordinateLink(link);

        if (have_output) {
            writeTextFile(output_path, rendered);
        } else {
            std::cout << rendered;
        }
        return 0;
    } catch (const cki::che_to_coord::ParseError& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
    }
    return 1;
}
