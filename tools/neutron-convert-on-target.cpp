// On-target Neutron model converter: dlopen the board's own libNeutronConverter.so
// and call converter::convertModel(vector<uint8>, string target) -> vector<uint8>.
// Microcode built by the board's converter matches the board's driver/firmware.
#include <dlfcn.h>
#include <cstdio>
#include <string>
#include <vector>

using ConvertFn = std::vector<unsigned char> (*)(const std::vector<unsigned char>&,
                                                 const std::string&);
// mangled: converter::convertModel(vector<unsigned char> const&, string const&)
static const char* kConvertSym =
    "_ZN9converter12convertModelERKSt6vectorIhSaIhEERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE";
static const char* kListSym = "_Z23printNeutronTargetNamesv";  // printNeutronTargetNames()

int main(int argc, char** argv) {
    const char* lib = "/run/media/root-mmcblk0p2/usr/lib/libNeutronConverter.so";
    if (argc >= 2 && std::string(argv[1]) == "--lib") { lib = argv[2]; argv += 2; argc -= 2; }

    void* h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { std::fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 1; }

    if (argc >= 2 && std::string(argv[1]) == "--list") {
        auto f = reinterpret_cast<void (*)()>(dlsym(h, kListSym));
        if (!f) { std::fprintf(stderr, "no target-list symbol\n"); return 1; }
        std::printf("Targets supported by %s:\n", lib);
        f();
        return 0;
    }
    if (argc < 4) {
        std::fprintf(stderr,
            "usage: %s [--lib <.so>] <in.tflite> <out.tflite> <target>\n"
            "       %s [--lib <.so>] --list\n", argv[0], argv[0]);
        return 2;
    }
    const char* in = argv[1]; const char* out = argv[2]; std::string target = argv[3];

    auto convertModel = reinterpret_cast<ConvertFn>(dlsym(h, kConvertSym));
    if (!convertModel) { std::fprintf(stderr, "convertModel symbol not found\n"); return 1; }

    FILE* f = std::fopen(in, "rb");
    if (!f) { std::perror(in); return 1; }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> data(n);
    size_t got = std::fread(data.data(), 1, (size_t)n, f); std::fclose(f);
    if ((long)got != n) { std::fprintf(stderr, "read error\n"); return 1; }

    std::fprintf(stderr, "converting %ld bytes for target '%s' (board converter)...\n", n,
                 target.c_str());
    std::vector<unsigned char> res = convertModel(data, target);
    std::fprintf(stderr, "converted -> %zu bytes\n", res.size());

    FILE* o = std::fopen(out, "wb");
    if (!o) { std::perror(out); return 1; }
    std::fwrite(res.data(), 1, res.size(), o); std::fclose(o);
    std::fprintf(stderr, "wrote %s\n", out);
    return 0;
}
