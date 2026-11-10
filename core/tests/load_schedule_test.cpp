#include "forearm/load_schedule.h"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if (!(x)) {std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1);} } while (0)
int main() {
    forearm::LoadSchedule load;
    CHECK(!load.start(0,1000,0)); CHECK(!load.start(101,1000,0));
    CHECK(!load.start(20,0,0)); CHECK(!load.start(20,600001,0));
    CHECK(load.start(20,600000,1000));
    CHECK(load.tick(999)==0); CHECK(load.tick(1000)==0); CHECK(load.tick(1049)==0);
    CHECK(load.tick(1050)==1); CHECK(load.tick(1050)==0);
    for (unsigned t=1100;t<=601000;t+=50) CHECK(load.tick(t)==1);
    CHECK(!load.active()); CHECK(load.generated()==12000); CHECK(load.dropped()==0);
    CHECK(load.elapsed(999999)==600000); CHECK(load.tick(999999)==0);
    CHECK(load.start(100,5000,0));
    CHECK(load.tick(1000)==8); CHECK(load.generated()==8); CHECK(load.dropped()==92);
    CHECK(load.tick(1000)==0); CHECK(load.tick(1010)==1);
    CHECK(load.tick(5000)==8); CHECK(load.generated()==17); CHECK(load.dropped()==483);
    CHECK(!load.active()); CHECK(load.generated()+load.dropped()==500);
    CHECK(load.start(20,10000,100)); CHECK(load.tick(150)==1);
    load.stop(160); CHECK(!load.active()); CHECK(load.elapsed(90000)==60); CHECK(load.tick(90000)==0);
    CHECK(load.start(20,1000,0)); CHECK(load.elapsed(0)==0); CHECK(load.tick(1000)==8);
    CHECK(load.elapsed(5000)==1000); CHECK(load.generated()==8); CHECK(load.dropped()==12);
    std::cout << "bounded load scheduler passed\n";
}
