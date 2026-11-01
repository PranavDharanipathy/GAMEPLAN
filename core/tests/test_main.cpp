#include <cstring>
#include "testing.hpp"

int main(int argc, char** argv) {
    int fails = 0, run = 0;
    for (auto& c : gptest::registry()) {
        if (argc > 1 && !std::strstr(c.name, argv[1])) continue;
        ++run;
        try {
            c.fn();
            std::printf("[ PASS ] %s\n", c.name);
        } catch (gptest::Failure& f) {
            ++fails;
            std::printf("[ FAIL ] %s\n         %s\n", c.name, f.msg.c_str());
        } catch (std::exception& e) {
            ++fails;
            std::printf("[ FAIL ] %s\n         exception: %s\n", c.name, e.what());
        }
    }
    std::printf("\n%d/%d passed\n", run - fails, run);
    return fails ? 1 : 0;
}
