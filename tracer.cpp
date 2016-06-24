#include <iostream> // for cout/cin
#include <stdlib.h> // for exit codes
#include <unistd.h> // for usleep and fork
#include <sys/types.h> // for pid_t
#include <sys/wait.h> // for wait
#include <signal.h> // for kill
#include <fstream> //for file io
#include <cstring> //for strcmp
#include <stdlib.h> //stdtoull for uint64_t parsing
#include <sstream> //stringstream

#ifndef __APPLE__
#include <sys/ptrace.h> // for ptrace
#endif

typedef uint64_t addr;

void panic(std::string msg);
void log(std::string msg);
std::string addrToHex(addr a);

const std::string tracerHdr = "[Tracer]: ";
extern const char etext, edata, end;

class memMap
{
private:
    struct mapsData { addr stackTop, stackBot, heapTop, heapBot, textBot;};
    static mapsData getMaps(pid_t dummyPid)
    {
        mapsData md;

        std::ifstream dummyMemMaps;
        std::string dummyMemMapsPath = "/proc/" + std::to_string(dummyPid) + "/maps";
        char * pEnd;

        //read dummy's memory map
        dummyMemMaps.open(dummyMemMapsPath);
        if (dummyMemMaps.is_open())
        {
            std::string line;
            while (!dummyMemMaps.eof())
            {
                getline(dummyMemMaps, line);
                if (line.find("[stack]") != std::string::npos) //parse stack top and bottom
                {
                    std::stringstream ss;
                    int firstHyphen = line.find('-');
                    ss << std::hex << line.substr(0, firstHyphen + 1);
                    ss >> md.stackTop;
                    ss.str("");
                    ss << std::hex << line.substr(firstHyphen + 1, line.find(' ') - firstHyphen);
                    ss >> md.stackBot;
                }
                else if (line.find("[heap]") != std::string::npos) //parse heap top and bottom
                {
                    std::stringstream ss;
                    int firstHyphen = line.find('-');
                    ss << std::hex << line.substr(0, firstHyphen + 1);
                    ss >> md.heapTop;
                    ss.str("");
                    ss << std::hex << line.substr(firstHyphen + 1, line.find(' ') - firstHyphen);
                    ss >> md.heapBot;
                }
                else if (line.find("r-xp") != std::string::npos && line.find("dummy") != std::string::npos)
                {
                    std::stringstream ss;
                    int firstHyphen = line.find('-');
                    ss << std::hex << line.substr(0, firstHyphen + 1);
                    ss >> md.textBot;
                }
            }
        }
        else
        {
            panic("Could not open " + dummyMemMapsPath + "!\n");
        }
        dummyMemMaps.close();

        return md;
    }
public:
    const mapsData maps;
    const addr uDataTop = (addr) &end, iDataTop = (addr) &edata, textTop = (addr) &etext;
    memMap(pid_t dummyPid) : maps(getMaps(dummyPid)) {};
};

class Tracer {

private:
    pid_t dummyPid;

    void tracerProcess() {
        //first make sure that ASLR is turned off - otherwise /proc/<pid>/maps won't make sense!!!
#ifndef __APPLE__
        std::ifstream aslrSetting;
        std::string aslrSettingPath = "/proc/sys/kernel/randomize_va_space";
        aslrSetting.open(aslrSettingPath);
        if (aslrSetting.is_open())
        {
            std::string line;
            if (!aslrSetting.eof())
            {
                getline(aslrSetting, line);
                bool no_aslr = (line.compare("0") == 0);
                log("Is ASLR setting off: " + std::string(no_aslr ? "yes" : "no") + "\n");

                if (!no_aslr) {
                    tracerPanic("ASLR must be turned off! Read line: " + line + "\n");
                }
            }
        }
        else
        {
            tracerPanic("Could not open " + aslrSettingPath + "!\n");
        }
        aslrSetting.close();
#endif

        const int msWait = 2500;

        while (1)
        {
            log("Dummy PID: " + std::to_string(dummyPid) + "\n");

            //wreck


            //trace stuff
            // memMap mm(dummyPid);
            // log("Stack Top: 0x" + addrToHex(mm.maps.stackTop) + "\n");
            // log("Stack Bot: 0x" + addrToHex(mm.maps.stackBot) + "\n");
            // log("Heap Top: 0x" + addrToHex(mm.maps.heapTop) + "\n");
            // log("Heap Bot: 0x" + addrToHex(mm.maps.heapBot) + "\n");
            // log("uData Top: 0x" + addrToHex(mm.uDataTop) + "\n");
            // log("iData Top: 0x" + addrToHex(mm.iDataTop) + "\n");
            // log("Text Top: 0x" + addrToHex(mm.textTop) + "\n");
            // log("Text Bot: 0x" + addrToHex(mm.maps.textBot) + "\n");

            if (waitpid(dummyPid, 0, WNOHANG))
            {
                log("Dummy has stopped...\n");
                break;
            }

            usleep(msWait * 1000);

#ifdef _CLEAR_SCR
            // std::system("clear");
#endif
        }

        waitpid(dummyPid, 0, 0); // wait for child to exit
    }

    void startChildProcess()
    {
        const std::string dummyPath = "./dummy.exe";

        char* const* cmdArgs = NULL;

        execv(dummyPath.c_str(), cmdArgs);

        tracerPanic("Child process exiting via parent!?!?\n");
    }

    void tracerPanic(std::string msg)
    {
        kill(dummyPid, SIGKILL);
        panic(msg);
    }

public:
    void run()
    {
        log("-+-+-Tracer Starts-+-+-\n");
        log("Starting dummy...\n");

        dummyPid = fork();

        if (dummyPid == 0) //this checks for child process
            startChildProcess();
        else
            tracerProcess();

        log("-+-+-Tracer Ends-+-+-\n");
    }
};

void panic(std::string msg)
{
    std::cerr << tracerHdr << "Fatal Error! " << msg;
    exit(EXIT_FAILURE);
}

void log(std::string msg)
{
    std::cout << tracerHdr << msg;
}

std::string addrToHex(addr a)
{
    std::stringstream ss;
    ss << std::hex << a;
    return ss.str();
}

int main(int argc, char ** argv)
{
    Tracer t;
    t.run();
    return EXIT_SUCCESS;
}