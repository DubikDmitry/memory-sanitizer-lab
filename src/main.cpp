#include <iostream>
#include <limits>
#include <memory>
#include <string>

namespace {

void heap_buffer_overflow(bool fixed) {
    constexpr int kCount = 3;
    auto values = std::make_unique<int[]>(kCount);

    if (fixed) {
        values[kCount - 1] = 42;
        std::cout << "Fixed: wrote to the last valid array element.\n";
    } else {
        // Intentional defect for the ASan demonstration: valid indexes are 0, 1, 2.
        values[kCount] = 42;
        std::cout << "This line should not be reached with ASan enabled.\n";
    }
}

void use_after_free(bool fixed) {
    int* value = new int(123);

    if (fixed) {
        const int saved_value = *value;
        delete value;
        std::cout << "Fixed: saved the value before releasing memory: "
                  << saved_value << '\n';
    } else {
        delete value;
        // Intentional defect for the ASan demonstration: value is no longer valid.
        volatile int read_after_delete = *value;
        (void)read_after_delete;
    }
}

void signed_integer_overflow(bool fixed) {
    const int maximum = std::numeric_limits<int>::max();
    constexpr int increment = 1;

    if (fixed) {
        const long long result = static_cast<long long>(maximum) + increment;
        std::cout << "Fixed: calculated in a wider type: " << result << '\n';
    } else {
        // Read operands through volatile objects so the addition is evaluated at runtime.
        // Intentional defect for the UBSan demonstration: signed overflow is undefined.
        volatile int left_operand = maximum;
        volatile int right_operand = increment;
        volatile int result = left_operand + right_operand;
        (void)result;
    }
}

void memory_leak(bool fixed) {
    if (fixed) {
        auto values = std::make_unique<int[]>(64);
        values[0] = 7;
        std::cout << "Fixed: memory is owned by unique_ptr and released automatically.\n";
    } else {
        int* values = new int[64];
        values[0] = 7;
        // Intentional defect: the owning pointer is lost without delete[].
        (void)values;
        std::cout << "Intentional leak created; the process is about to exit.\n";
    }
}

void print_usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " <scenario>\n\n"
        << "Defective scenarios (run separately; sanitizer is expected to report):\n"
        << "  oob       heap buffer overflow (ASan)\n"
        << "  uaf       heap use-after-free (ASan)\n"
        << "  overflow  signed integer overflow (UBSan)\n"
        << "  leak      memory leak (LeakSanitizer / Valgrind)\n\n"
        << "Corrected scenarios (expected to finish cleanly):\n"
        << "  oob-fixed\n"
        << "  uaf-fixed\n"
        << "  overflow-fixed\n"
        << "  leak-fixed\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 2;
    }

    const std::string scenario = argv[1];

    if (scenario == "oob") {
        heap_buffer_overflow(false);
    } else if (scenario == "oob-fixed") {
        heap_buffer_overflow(true);
    } else if (scenario == "uaf") {
        use_after_free(false);
    } else if (scenario == "uaf-fixed") {
        use_after_free(true);
    } else if (scenario == "overflow") {
        signed_integer_overflow(false);
    } else if (scenario == "overflow-fixed") {
        signed_integer_overflow(true);
    } else if (scenario == "leak") {
        memory_leak(false);
    } else if (scenario == "leak-fixed") {
        memory_leak(true);
    } else {
        print_usage(argv[0]);
        return 2;
    }

    return 0;
}
