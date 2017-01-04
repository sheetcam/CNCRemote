#include <iostream>
#include <cstdint>
#include <time.h>

#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <sys/types.h>

#include "getopt.h"
#include "linuxcnc.h"
#include "millisleep.h"

#include "rcs.hh"
#include "posemath.h"		// PM_POSE, TO_RAD
#include "emc.hh"		// EMC NML
#include "canon.hh"		// CANON_UNITS, CANON_UNITS_INCHES,MM,CM
#include "emcglb.h"		// EMC_NMLFILE, TRAJ_MAX_VELOCITY, etc.
#include "emccfg.h"		// DEFAULT_TRAJ_MAX_VELOCITY
#include "inifile.hh"		// INIFILE
#include "rcs_print.hh"
#include "timer.hh"             // etime()
#include "shcom.hh"

char port[PATH_MAX] = {DEFAULT_COMMS_PORT};

struct option longopts[] =
{
    {"help", 0, NULL, 'h'},
    {"port", 1, NULL, 'p'},
    {"path", 1, NULL, 'd'},
    {NULL,0,NULL,0}
};



using namespace std;



static void Disconnect()
{
//    EMC_NULL emc_null_msg;

    if (emcStatusBuffer != 0) // wait until current message has been received
    {
        emcCommandWaitReceived();
    }

    delete emcErrorBuffer;
    emcErrorBuffer = 0;
    delete emcStatusBuffer;
    emcStatusBuffer = 0;
    emcStatus = 0;
    delete emcCommandBuffer;
    emcCommandBuffer = 0;
}


static void sigQuit(int sig)
{
    Disconnect();
    exit(0);
}


static void initMain()
{
    emcWaitType = EMC_WAIT_RECEIVED;
    emcCommandSerialNumber = 0;
    emcTimeout = 0.0;
    emcUpdateType = EMC_UPDATE_AUTO;
    linearUnitConversion = LINEAR_UNITS_AUTO;
    angularUnitConversion = ANGULAR_UNITS_AUTO;
    emcCommandBuffer = 0;
    emcStatusBuffer = 0;
    emcStatus = 0;

    emcErrorBuffer = 0;
    error_string[LINELEN-1] = 0;
    operator_text_string[LINELEN-1] = 0;
    operator_display_string[LINELEN-1] = 0;
    programStartLine = 0;
}

static void usage(char* pname)
{
    printf("Usage: \n");
    printf("         %s [Options] [-- LinuxCNC_Options]\n"
           "Options:\n"
           "         --help       this help\n"
           "         --port       <port number>  (default=%s)\n"
           "         --path       <path>         (default=%s)\n"
           "LinuxCNC_Options:\n"
           "          -ini        <inifile>      (default=%s)\n"
           ,pname,port,defaultPath,emc_inifile
          );
}



int main(int argc, char * argv[])
{
    int opt;

    initMain();
    // process local command line args
    while((opt = getopt_long(argc, argv, "he:p:d:", longopts, NULL)) != - 1)
    {
        switch(opt)
        {
        case 'h':
            usage(argv[0]);
            exit(1);
        case 'p':
            strncpy(port, optarg, strlen(optarg) + 1);
            break;
        case 'd':
            strncpy(defaultPath, optarg, strlen(optarg) + 1);
            break;
        }
    }

    // process LinuxCNC command line args
    // Note: '--' may be used to separate cmd line args
    //       optind is index of next arg to process
    //       make argv[optind] zeroth arg
    argc = argc - optind + 1;
    argv = argv + optind - 1;
    if (emcGetArgs(argc, argv) != 0)
    {
        rcs_print_error("error in argument list\n");
        exit(1);
    }

    // attach our quit function to SIGINT
    {
        struct sigaction act;
        act.sa_handler = sigQuit;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        sigaction(SIGINT, &act, NULL);
    }


    LinuxCnc machine;
    string t = "tcp://*:";
    t += port;
    if(machine.Connect(t) != CncRemote::Comms::errOK)
    {
        printf("Failed to bind port %s. Is another server already running on that port?\n", port);
        return -1;
    }
    while(1)
    {
        // get configuration information
        iniLoad(emc_inifile);
        machine.ConnectLCnc();
        while(machine.Poll())
        {
            sleep_ms(2);
        }
        Disconnect();
    }
    return 0;
}