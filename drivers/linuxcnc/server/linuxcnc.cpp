#include "linuxcnc.h"


#include "rcs.hh"
#include "posemath.h"		// PM_POSE, TO_RAD
#include "emc.hh"		// EMC NML
#include "canon.hh"		// CANON_UNITS, CANON_UNITS_INCHES,MM,CM
#include "emcglb.h"		// EMC_NMLFILE, TRAJ_MAX_VELOCITY, etc.
#include "emccfg.h"		// DEFAULT_TRAJ_MAX_VELOCITY
//#include "inifile.hh"		// INIFILE
#include "rcs_print.hh"
#include "timer.hh"             // etime()
#include "shcom.hh"             // NML Messaging functions

#include "shcom.cc" //this way we can use the include search path to find shcom.cc

#define LINUXCNCVER 27


LinuxCnc::LinuxCnc()
{
    m_slowCount = 0;
    m_heartbeat = 0;
    m_nextTime = 0;
    m_connected = false;

//    IniFile inifile;
//    const char *inistring;
    m_maxSpeedLin = 4000;
    m_maxSpeedAng = 100;
/*
    // open it
    if (inifile.Open(emc_inifile) == false)
    {
        return;
    }
    float tmp = 0;
    if (NULL != (inistring = inifile.Find("MAX_LINEAR_VELOCITY", "DISPLAY")))
    {
        // copy to global
        if (1 != sscanf(inistring, "%f", &tmp))
        {
            tmp = 0;
        }
    }
    if (tmp == 0 && NULL != (inistring = inifile.Find("MAX_LINEAR_VELOCITY", "TRAJ")))
    {
        // copy to global
        if (1 != sscanf(inistring, "%f", &tmp))
        {
            tmp = 0;
        }
    }
    if(tmp != 0)
    {
        m_maxSpeedLin = tmp;
    }

    tmp = 0;
    if (NULL != (inistring = inifile.Find("MAX_ANGULAR_VELOCITY", "DISPLAY")))
    {
        // copy to global
        if (1 != sscanf(inistring, "%f", &tmp))
        {
            tmp = 0;
        }
    }
    if (tmp == 0 && NULL != (inistring = inifile.Find("MAX_ANGULAR_VELOCITY", "TRAJ")))
    {
        // copy to global
        if (1 != sscanf(inistring, "%f", &tmp))
        {
            tmp = 0;
        }
    }
    if(tmp != 0)
    {
        m_maxSpeedAng = tmp;
    }
    */
}

void LinuxCnc::ConnectLCnc()
{
    while(tryNml(0.5,0.5) !=0)
    {
        set_machine_connected(false);
        Server::Poll();
    }

    // init NML
    // get current serial number, and save it for restoring when we quit
    // so as not to interfere with real operator interface
    updateStatus();

    if(emcStatus->size != sizeof(EMC_STAT))
    {
        printf("Wrong LinuxCNC version");
        exit(0);
    }


    m_maxSpeedLin = emcStatus->motion.traj.maxVelocity;
    m_maxSpeedAng = emcStatus->motion.traj.maxVelocity;



    emcCommandSerialNumber = emcStatus->echo_serial_number;
    m_heartbeat = emcStatus->task.heartbeat;
    m_nextTime = time(NULL) + 1; //check every second
    m_connected = true;
#if LINUXCNCVER < 28
#define EMC_WAIT_NONE (EMC_WAIT_TYPE) 1
#endif // LINUXCNCVER
    emcWaitType = EMC_WAIT_NONE;
}

#include "timer.h"

bool LinuxCnc::Poll()
{
    if(updateStatus() != 0)
    {
        printf("Disconnected\n");
        Disconnect();
        return false;
    }
    Server::Poll();
    return m_connected;
}

void LinuxCnc::UpdateState()
{
    Clear();
    if(emcStatus == NULL)
    {
        set_machine_connected(false);
        return;
    }
    CncRemote::Axes& axes = *mutable_abs_pos();
#if LINUXCNCVER < 28
    axes.set_x(emcStatus->motion.traj.actualPosition.tran.x / emcStatus->motion.axis[0].units);
    axes.set_y(emcStatus->motion.traj.actualPosition.tran.y / emcStatus->motion.axis[1].units);
    axes.set_z(emcStatus->motion.traj.actualPosition.tran.z / emcStatus->motion.axis[2].units);
    axes.set_a(emcStatus->motion.traj.actualPosition.a / emcStatus->motion.axis[3].units);
    axes.set_b(emcStatus->motion.traj.actualPosition.b / emcStatus->motion.axis[4].units);
    axes.set_c(emcStatus->motion.traj.actualPosition.c / emcStatus->motion.axis[5].units);
#else
    axes.set_x(emcStatus->motion.traj.actualPosition.tran.x / emcStatus->motion.joint[0].units);
    axes.set_y(emcStatus->motion.traj.actualPosition.tran.y / emcStatus->motion.joint[1].units);
    axes.set_z(emcStatus->motion.traj.actualPosition.tran.z / emcStatus->motion.joint[2].units);
    axes.set_a(emcStatus->motion.traj.actualPosition.a / emcStatus->motion.joint[3].units);
    axes.set_b(emcStatus->motion.traj.actualPosition.b / emcStatus->motion.joint[4].units);
    axes.set_c(emcStatus->motion.traj.actualPosition.c / emcStatus->motion.joint[5].units);
#endif

    switch (m_slowCount++) //these don't need to be updated very fast so only send one per frame
    {
    case 1:
        {
            static bool prevState = false;
            bool state = emcStatus->task.state == EMC_TASK_STATE_ON;
            if(state != prevState)
            {
                ZeroJog();
                prevState = state;
            }
            set_control_on(state);
        }
        set_machine_connected(true);
        break;

    case 2:
        switch(emcStatus->motion.spindle.enabled)
        {
        case 0:
            set_spindle_state(CncRemote::spinOFF);
            break;

        case -1:
            set_spindle_state(CncRemote::spinREV);
            break;

        case 1:
            set_spindle_state(CncRemote::spinFWD);
            break;
        }
        set_spindle_speed(emcStatus->motion.spindle.speed);
        break;

    case 3:
        set_paused(emcStatus->task.task_paused);
        set_max_feed_lin((m_maxSpeedLin * 60) / emcStatus->motion.axis[0].units);
        set_max_feed_ang((m_maxSpeedAng * 60) / emcStatus->motion.axis[0].units);
        break;

    case 4:
        set_optional_stop(emcStatus->task.optional_stop_state);
        set_block_delete(emcStatus->task.block_delete_state);
        break;

    case 5:
        set_mist(emcStatus->io.coolant.mist);
        set_flood(emcStatus->io.coolant.flood);
        break;

    case 6:
        {
            static bool prevState = false;
            bool state = emcStatus->task.interpState != EMC_TASK_INTERP_IDLE;
            if(state != prevState)
            {
                ZeroJog();
                prevState = state;
            }
            set_running(state);
        }

        updateError();
        if(error_string[0] != 0)
        {
            set_error_msg(error_string);
            error_string[0] = 0;
        }
        if(operator_text_string[0] != 0)
        {
            set_display_msg(operator_text_string);
            operator_text_string[0] = 0;
        }
        else if(operator_display_string[0] != 0)
        {
            set_display_msg(operator_display_string);
            operator_display_string[0] = 0;
        }
        break;

    case 7:
        set_current_line(emcStatus->task.motionLine);
        break;

    case 8:
    {
        CncRemote::Axes& axes = *mutable_offset_fixture();
#if LINUXCNCVER < 28
        axes.set_x(emcStatus->task.g5x_offset.tran.x / emcStatus->motion.axis[0].units);
        axes.set_y(emcStatus->task.g5x_offset.tran.y / emcStatus->motion.axis[1].units);
        axes.set_z(emcStatus->task.g5x_offset.tran.z / emcStatus->motion.axis[2].units);
        axes.set_a(emcStatus->task.g5x_offset.a / emcStatus->motion.axis[3].units);
        axes.set_b(emcStatus->task.g5x_offset.b / emcStatus->motion.axis[4].units);
        axes.set_c(emcStatus->task.g5x_offset.c / emcStatus->motion.axis[5].units);
#else
        axes.set_x(emcStatus->task.g5x_offset.tran.x / emcStatus->motion.joint[0].units);
        axes.set_y(emcStatus->task.g5x_offset.tran.y / emcStatus->motion.joint[1].units);
        axes.set_z(emcStatus->task.g5x_offset.tran.z / emcStatus->motion.joint[2].units);
        axes.set_a(emcStatus->task.g5x_offset.a / emcStatus->motion.joint[3].units);
        axes.set_b(emcStatus->task.g5x_offset.b / emcStatus->motion.joint[4].units);
        axes.set_c(emcStatus->task.g5x_offset.c / emcStatus->motion.joint[5].units);
#endif
    }
    break;

    case 9:
    {
        CncRemote::Axes& axes = *mutable_offset_work();
#if LINUXCNCVER < 28
        axes.set_x(emcStatus->task.g92_offset.tran.x / emcStatus->motion.axis[0].units);
        axes.set_y(emcStatus->task.g92_offset.tran.y / emcStatus->motion.axis[1].units);
        axes.set_z(emcStatus->task.g92_offset.tran.z / emcStatus->motion.axis[2].units);
        axes.set_a(emcStatus->task.g92_offset.a / emcStatus->motion.axis[3].units);
        axes.set_b(emcStatus->task.g92_offset.b / emcStatus->motion.axis[4].units);
        axes.set_c(emcStatus->task.g92_offset.c / emcStatus->motion.axis[5].units);
#else
        axes.set_x(emcStatus->task.g92_offset.tran.x / emcStatus->motion.joint[0].units);
        axes.set_y(emcStatus->task.g92_offset.tran.y / emcStatus->motion.joint[1].units);
        axes.set_z(emcStatus->task.g92_offset.tran.z / emcStatus->motion.joint[2].units);
        axes.set_a(emcStatus->task.g92_offset.a / emcStatus->motion.joint[3].units);
        axes.set_b(emcStatus->task.g92_offset.b / emcStatus->motion.joint[4].units);
        axes.set_c(emcStatus->task.g92_offset.c / emcStatus->motion.joint[5].units);
#endif
    }
    break;

    case 10:
    {
        CncRemote::BoolAxes& axes = *mutable_homed();
#if LINUXCNCVER < 28
        axes.set_x(emcStatus->motion.axis[0].homed);
        axes.set_y(emcStatus->motion.axis[1].homed);
        axes.set_z(emcStatus->motion.axis[2].homed);
        axes.set_a(emcStatus->motion.axis[3].homed);
        axes.set_b(emcStatus->motion.axis[4].homed);
        axes.set_c(emcStatus->motion.axis[5].homed);
#else
        axes.set_x(emcStatus->motion.joint[0].homed);
        axes.set_y(emcStatus->motion.joint[1].homed);
        axes.set_z(emcStatus->motion.joint[2].homed);
        axes.set_a(emcStatus->motion.joint[3].homed);
        axes.set_b(emcStatus->motion.joint[4].homed);
        axes.set_c(emcStatus->motion.joint[5].homed);
#endif
    }
    break;

    default:
        if(time(NULL) > m_nextTime)
        {
            if(m_heartbeat != emcStatus->task.heartbeat)
            {
                m_heartbeat = emcStatus->task.heartbeat;
            }
            else
            {
                m_connected = false;
            }
            m_nextTime = time(NULL) + 1; //check every second
        }
        m_slowCount = 1;
    }
}

inline void LinuxCnc::SendJog(const int axis, const double vel)
{
    if(vel == m_jogAxes[axis]) return;
//printf("Jog %f\n", vel);
    if(vel != 0)
    {
        EMC_AXIS_JOG emc_axis_jog_msg;
        emc_axis_jog_msg.axis = axis;
        emc_axis_jog_msg.vel = vel;
        emcCommandSend(emc_axis_jog_msg);
    }else
    {
        EMC_AXIS_ABORT emc_axis_abort_msg;
        emc_axis_abort_msg.axis = axis;
        emcCommandSend(emc_axis_abort_msg);
    }
    m_jogAxes[axis] = vel;
}

void LinuxCnc::ZeroJog()
{
    memset(m_jogAxes, 0, sizeof(m_jogAxes));
}

int LinuxCnc::SendJogVel(const double x, const double y, const double z, const double a, const double b, const double c)
{
    if (emcStatus->motion.traj.mode != EMC_TRAJ_MODE_TELEOP)
    {
        SendJog(0,x * m_maxSpeedLin);
        SendJog(1,y * m_maxSpeedLin);
        SendJog(2,z * m_maxSpeedLin);
        SendJog(3,a * m_maxSpeedAng);
        SendJog(4,b * m_maxSpeedAng);
        SendJog(5,c * m_maxSpeedAng);
        return 0;
    }


    EMC_TRAJ_SET_TELEOP_VECTOR emc_set_teleop_vector;
    ZERO_EMC_POSE(emc_set_teleop_vector.vector);
#if LINUXCNCVER < 28
    emc_set_teleop_vector.vector.tran.x = x * m_maxSpeedLin;
    emc_set_teleop_vector.vector.tran.y = y * m_maxSpeedLin;
    emc_set_teleop_vector.vector.tran.z = z * m_maxSpeedLin;
    emc_set_teleop_vector.vector.a = a * m_maxSpeedAng;
    emc_set_teleop_vector.vector.b = b * m_maxSpeedAng;
    emc_set_teleop_vector.vector.c = c * m_maxSpeedAng;
#else
    emc_set_teleop_vector.vector.tran.x = x * scale * emcStatus->motion.joint[0].units;
    emc_set_teleop_vector.vector.tran.y = y * scale * emcStatus->motion.joint[1].units;
    emc_set_teleop_vector.vector.tran.z = z * scale * emcStatus->motion.joint[2].units;
    emc_set_teleop_vector.vector.a = a * scale * emcStatus->motion.joint[3].units;
    emc_set_teleop_vector.vector.b = b * scale * emcStatus->motion.joint[4].units;
    emc_set_teleop_vector.vector.c = c * scale * emcStatus->motion.joint[5].units;
#endif


    emcCommandSend(emc_set_teleop_vector);

//    if (emcWaitType == EMC_WAIT_RECEIVED)
    {
//        return emcCommandWaitReceived();
    }
/*    else if (emcWaitType == EMC_WAIT_DONE)
    {
        return emcCommandWaitDone();
    }*/

    return 0;
    /*



    #if LINUXCNCVER < 28
        sendJogCont(axis, val * emcStatus->motion.axis[axis].units);
    #else
        if(emcStatus->motion.joint[axis].jointType == EMC_LINEAR)
            sendJogCont(axis,JOGTELEOP, val * emcStatus->motion.joint[axis].units);
        else
            sendJogCont(axis,JOGTELEOP, val * emcStatus->motion.joint[axis].units);
    #endif*/
}


void LinuxCnc::SendJogStep(const int axis, const double val)
{
#if LINUXCNCVER < 28
    if(emcStatus->motion.axis[axis].axisType == EMC_AXIS_LINEAR)
        sendJogIncr(axis, val * emcStatus->motion.axis[0].units, m_maxSpeedLin);
    else
        sendJogIncr(axis, val * emcStatus->motion.axis[0].units, m_maxSpeedAng);
#else
    if(emcStatus->motion.joint[axis].jointType == EMC_LINEAR)
        sendJogIncr(axis,JOGTELEOP, val * emcStatus->motion.joint[0].units, m_maxSpeedLin);
    else
        sendJogIncr(axis,JOGTELEOP, val * emcStatus->motion.joint[0].units, m_maxSpeedAng);
#endif
}

void LinuxCnc::HandlePacket(const Packet & pkt)
{
    CncRemote::CmdBuf cmd;
    cmd.ParseFromString(pkt.data);
    if(emcStatus == NULL) return;
    switch(pkt.cmd)
    {
    case cmdDRIVESON:
        if(cmd.state())
        {
            sendEstopReset();
            sendMachineOn();
        }
        else
        {
            sendEstop();
        }
        break;

    case cmdJOGVEL:
        SetMode(EMC_TASK_MODE_MANUAL);
//        sendSetTeleopEnable(true);
        {
            const CncRemote::Axes& axes = cmd.axes();
            SendJogVel(axes.x(), axes.y(), axes.z(), axes.a(), axes.b(), axes.c());
        }
        break;

    case cmdJOGSTEP:
        SetMode(EMC_TASK_MODE_MANUAL);
        sendSetTeleopEnable(true);
        {
            const CncRemote::Axes& axes = cmd.axes();
            SendJogStep(0,axes.x());
            SendJogStep(1,axes.y());
            SendJogStep(2,axes.z());
            SendJogStep(3,axes.a());
            SendJogStep(4,axes.b());
            SendJogStep(5,axes.c());
        }
        break;

    case cmdMDI:
        SetMode(EMC_TASK_MODE_MDI);
        sendMdiCmd(cmd.string().c_str());
        break;

    case cmdFRO:
        sendFeedOverride(cmd.rate());
        break;

    case cmdFILE:
        sendProgramOpen((char *)(cmd.string().c_str()));
        break;

    case cmdSTART:
        SetMode(EMC_TASK_MODE_AUTO);
        sendProgramRun(0);
        break;

    case cmdSTOP:
        sendAbort();
        break;

    case cmdPAUSE:
        if(cmd.state())
        {
            sendProgramPause();
        }
        else
        {
            sendProgramResume();
        }
        break;

    case cmdBLOCKDEL:
        break;

    case cmdSINGLESTEP:
        sendProgramStep();
        break;

    case cmdOPTSTOP:
        sendSetOptionalStop(cmd.state());
        break;

    case cmdHOME:
    {
        SetMode(EMC_TASK_MODE_MANUAL);
        sendSetTeleopEnable(false);
        const CncRemote::BoolAxes& axes = cmd.bool_axes();
        if(axes.x() && axes.y() && axes.z())
        {
            sendHome(-1); //home all
            break;
        }
        if(axes.x())
        {
            sendHome(0);
            break;
        }
        if(axes.y())
        {
            sendHome(1);
            break;
        }
        if(axes.z())
        {
            sendHome(2);
            break;
        }
        if(axes.a())
        {
            sendHome(3);
            break;
        }
        if(axes.b())
        {
            sendHome(4);
            break;
        }
        if(axes.c())
        {
            sendHome(5);
            break;
        }
//        sendSetTeleopEnable(true);
    }
    break;

    case cmdSPINDLE:
        switch(cmd.intval())
        {
        case CncRemote::spinOFF:
            sendSpindleOff();
            break;

        case CncRemote::spinFWD:
            sendSpindleForward();
            break;

        case CncRemote::spinREV:
            sendSpindleReverse();
            break;
        }
        break;


    case cmdFLOOD:
        if(cmd.state())
        {
            sendFloodOn();
        }
        else
        {
            sendFloodOff();
        }
        break;

    case cmdMIST:
        if(cmd.state())
        {
            sendMistOn();
        }
        else
        {
            sendMistOff();
        }
        break;

    }
}

void LinuxCnc::SetMode(const int mode)
{
    if(emcStatus->task.mode == mode) return;
    switch(mode)
    {
    case EMC_TASK_MODE_MANUAL:
        sendManual();
        break;

    case EMC_TASK_MODE_AUTO:
        sendAuto();
        break;

    case EMC_TASK_MODE_MDI:
        sendMdi();
        break;
    }
}



