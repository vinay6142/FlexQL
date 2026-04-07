#include <iostream>
#include <sstream>
#include <chrono>
using namespace std;
using namespace std::chrono;
int main() {
    auto t1 = high_resolution_clock::now();
    stringstream ss;
    ss << "INSERT INTO BIG_USERS VALUES ";
    for(long long id = 1; id <= 100000; ++id) {
        ss << "(" << id << ", 'user" << id << "', 'user" << id << "@mail.com', " << (1000.0 + (id % 10000)) << ", 1893456000)";
        if(id < 100000) ss << ",";
    }
    ss << ";";
    string s = ss.str();
    auto t2 = high_resolution_clock::now();
    cout << duration_cast<milliseconds>(t2 - t1).count() << " ms for 100k" << endl;
    return 0;
}
