#include <iostream>
#include <sstream>
#include <unistd.h>
#include <ios>

extern const char etext, edata, end;

std::string longToHex(uint64_t a)
{
    std::stringstream ss;
    ss << std::hex << a;
    return ss.str();
}

class Dummy
{
private:
    const std::string dummyHdr = "\t[Dummy] ";
    const std::string meStr = "Corrupt me";
    const int msWait = 500;
    const int leakAmount = 300000; //number of ints to leak
    const int paraLeaks = 16;

    void panic(std::string msg)
    {
        std::cerr << dummyHdr << "Fatal Error! " << msg;
        exit(EXIT_FAILURE);
    }

    void log(std::string msg)
    {
        std::cout << dummyHdr << msg;
    }

    void leak(int **leakPtr, int meInt)
    {
        log(meStr + "\n");

        //leak memory on purpose
        #pragma omp for
        for (int i = 0; i < paraLeaks; i ++)
            *leakPtr = (int*) malloc(leakAmount * sizeof(int));

        std::stringstream ssLeak;
        ssLeak << *leakPtr;

        log("Loop #" + std::to_string(meInt) + "\n");
        meInt++;
        log("Leak at: " + ssLeak.str() + "\n");
        log("Currently leaking " + std::to_string(paraLeaks * meInt * leakAmount * 4 / 1024) + " KB\n");
        log("My mem segs: etext=0x" + longToHex((uint64_t) &etext) +
            ", edata=0x" + longToHex((uint64_t) &edata) +
            ", end=0x" + longToHex((uint64_t) &end) + "\n");

        usleep(msWait * 1000);

        if (meInt > -1)
            leak(leakPtr, meInt);
    }

public:
    void run()
    {
        log("-<-<-Dummy Starts->->-\n");

        int *leakPtr = 0;
        leak(&leakPtr, 0);

        log("-<-<-Dummy Ends->->-\n");
    }
};

int main(int argc, char ** argv) {
    Dummy d;
    d.run();

    return EXIT_SUCCESS;
}
