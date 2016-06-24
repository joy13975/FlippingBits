#include <iostream>
#include <sstream>
#include <unistd.h>
#include <ios>

class Dummy
{
private:
    const std::string dummyHdr = "\t[Dummy] ";
    const std::string meStr = "Brexit";
    const int msWait = 1000;
    const int leakAmount = 30000; //number of ints to leak

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

        log("Loop #" + std::to_string(meInt) + "\n");
        meInt++;

        //purposely leak memory
        *leakPtr = new int[leakAmount]();
        std::stringstream ssLeak;
        ssLeak << *leakPtr;
        log("Leak at: " + ssLeak.str() + "\n");
        log("Currently leaking " + std::to_string(meInt*leakAmount*4/1024) + " KB\n");

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
