#include <iostream>
#include <sstream>
#include <chrono>

using namespace std;
using namespace std::chrono;

int main() {
    auto bench_start = high_resolution_clock::now();

    long long inserted = 0;
    long long target_rows = 1000000;
    int INSERT_BATCH_SIZE = 1000000;

    stringstream ss;
    ss << "INSERT INTO BIG_USERS VALUES ";

    int in_batch = 0;
    while (in_batch < INSERT_BATCH_SIZE && inserted < target_rows) {
        long long id = inserted + 1;
        ss << "(" << id
           << ", 'user" << id << "'"
           << ", 'user" << id << "@mail.com'"
           << ", " << (1000.0 + (id % 10000))
           << ", 1893456000)";
        inserted++;
        in_batch++;
        if (in_batch < INSERT_BATCH_SIZE && inserted < target_rows) {
            ss << ",";
        }
    }
    ss << ";";
    string q = ss.str();

    auto bench_end = high_resolution_clock::now();
    cout << "Time: " << duration_cast<milliseconds>(bench_end - bench_start).count() << " ms\n";
    return 0;
}
