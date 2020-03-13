/*
 Name:		ZStage.ino
 Created:	3/8/2020 11:51:46 AM
 Author:	ThomasAles
*/
// Libraries
#include <TMC2130Stepper_UTILITY.h>
#include <TMC2130Stepper_REGDEFS.h>
#include <TMC2130Stepper.h>

// Globals
// SPI Chipselect pins for TMC2130 modules. Active low.
const int AX_CS[] = { 14, 21, 22 };
// Enable pins for TMC2130 modules. Active low.
const int AX_EN[] = { 15, 16, 17 };
// Step and direction pins
const int STEPPIN = 19;
const int DIRPIN = 20;
// Limit switch LEDs
const int LEDOUT[] = { 5, 6, 9 };
// Limit switch inputs
const int LIMPIN[] = { 1, 2, 3 };
// Flag for if any limit switch is triggered
volatile bool LIMSTATE = false;
// Global limit switch variable flags
volatile bool LIMITS[] = { false, false, false };
// Counters for the encoders.
// 0 = home
volatile int32_t AXES_COUNT[] = { 0, 0, 0 };

// Global serial flags
volatile bool MOVE_REQUESTED = false;

int REQUESTED_AXIS;
int REQUESTED_STEPS;
int REQUESTED_XTRA_US;
byte REQUESTED_DIR;

// Other Global Objects
TMC2130Stepper AXES[] = { TMC2130Stepper(AX_EN[0], DIRPIN, STEPPIN, AX_CS[0]),
                         TMC2130Stepper(AX_EN[1], DIRPIN, STEPPIN, AX_CS[1]),
                         TMC2130Stepper(AX_EN[2], DIRPIN, STEPPIN, AX_CS[2])
};

void move(int axis, int direction, int steps_to_move, int extra_delay = 0, int ignore_collision = false)
{
    switch (axis) {
        // Axis 0 corresponds to moving all axes in unison.
    case 0:
        for (int i = 0; i < 3; i++)
        {
            digitalWriteFast(AX_EN[i], LOW);
        }
        break;
    default:
        digitalWriteFast(AX_EN[axis - 1], LOW);   // need to offset by -1 to get proper array position
        break;
    }
    // Set the direction.
    if (direction == 0)
    {
        digitalWrite(DIRPIN, LOW);
    }
    else {
        digitalWrite(DIRPIN, HIGH);
    }
    // Begin moving
    int step_delay = 1200;
    if (ignore_collision == false)
    {
        for (int i = 0; i < steps_to_move; i++)
        {
            if (LIMSTATE == false)
            {
                digitalWriteFast(STEPPIN, LOW);
                delayMicroseconds(step_delay);
                digitalWriteFast(STEPPIN, HIGH);
                delayMicroseconds(step_delay);
            }
        }
        // Disable all motors after move.
        for (int i = 0; i < 3; i++)
        {
            digitalWriteFast(AX_EN[i], HIGH);
        }
    } else {
        // TODO: Implement ignore-collision routine for moving
        //       off of the limits if they're engaged.
        for (int i = 0; i < steps_to_move; i++)
        {
            digitalWriteFast(STEPPIN, LOW);
            delayMicroseconds(step_delay);
            digitalWriteFast(STEPPIN, HIGH);
            delayMicroseconds(step_delay);
        }

        // disable motors
        for (int i = 0; i < 3; i++)
        {
            digitalWriteFast(AX_EN[i], HIGH);
        }
    }
}

void home() {
    // steps to move before refreshing the while loop
    // effectively sets position resolution
    int _stepchunk = 20;
    // move all stages upward until a limit condition occurs
    while (LIMSTATE == false)
    {
        move(0, 0, _stepchunk);
        // increment global step counts
        noInterrupts();
        for (int i = 0; i < 3; i++)
        {
            AXES_COUNT[i] -= _stepchunk;
        }
        interrupts();
    }
}
/*  home_axis(int axis, int steps=100) - Homes a specific axis in 100 step
                                         increments until that limit is triggered.
                                         Purposely ignores LIMSTATE global so homing
                                         works while other axes are homed.
*/
void home_axis(int axis, int _stepchunk = 100)
{
    while (LIMITS[axis - 1] == false)
    {
        move(axis, 0, _stepchunk);
        AXES_COUNT[axis - 1] -= _stepchunk;
    }
    delay(50);
    noInterrupts();
    if (LIMITS[axis - 1] == true)
    {
        AXES_COUNT[axis - 1] = 0;
    }
    interrupts();
}
/*  limit_switch_interrupt() - ISR to set the LIMITS[] flag for respective
                               axis it is attached to. Attach to pin CHANGE
                               event for each limit switch in setup().
*/
void limit_switch_interrupt() {
    noInterrupts();
    for (int i = 0; i < 3; i++)
    {
        // Read the state of the limit pins.
        if (digitalReadFast(LIMPIN[i]))
        {
            LIMITS[i] = true;
            digitalWriteFast(LEDOUT[i], HIGH);
        }
        else {
            LIMITS[i] = false;
            digitalWriteFast(LEDOUT[i], LOW);
        }
    }
    // compute the global limit flag
    LIMSTATE = LIMITS[0] || LIMITS[1] || LIMITS[2];
    interrupts();
}

/*  setup() - Runs as soon as board power is applied.
*/
void setup() {
    // Attach the limit switch interrupt to each limit switch pin
    // and set the relevant pin modes.
    for (int i = 0; i < 3; i++)
    {
        pinMode(LEDOUT[i], OUTPUT); pinMode(LIMPIN[i], INPUT_PULLDOWN);
        pinMode(AX_EN[i], OUTPUT); pinMode(AX_CS[i], OUTPUT);
        digitalWriteFast(AX_EN[i], HIGH); digitalWriteFast(AX_CS[i], HIGH);
        attachInterrupt(LIMPIN[i], limit_switch_interrupt, CHANGE);
    }
    // Start serial connection
    Serial.begin(115200);
    Serial.println("SERINTOK");

    /* Initalize motors. Settings for the motors:
       700mA, No Stealthchop, Microstepping = 0
    */
    for (int i = 0; i < 3; i++) {
        AXES[i].begin();
        AXES[i].SilentStepStick2130(700);
        AXES[i].stealthChop(0);
        AXES[i].microsteps(0);
    }
    Serial.println("MOTINTOK");
}

/*  loop() - Runs immediately after setup(). Loops.
*/
void loop() {
    check_data();
    Serial.println(return_axis_steps());
    if (MOVE_REQUESTED)     // if the move_requested flag is set, move the axis
    {
        int _nocollision;
        if(REQUESTED_DIR == 1)
        { 
            _nocollision = 1;
        }
        else {
            _nocollision = 0;
        }
        move(REQUESTED_AXIS, REQUESTED_AXIS, REQUESTED_STEPS, REQUESTED_XTRA_US, _nocollision);
        // clear flag after successful move and set all globals to invalid values 
        // to avoid triggering a homing sequence or random move.
        MOVE_REQUESTED = false;
        REQUESTED_AXIS = -1; REQUESTED_DIR = -1; REQUESTED_STEPS = -1; REQUESTED_XTRA_US = -1;
    }
    delay(500);
    /* Homing Test Code BEGIN
    move(0, 1, 3000, 0);
    Serial.println("Attempting to find common plane");
    home();
    for (int i = 0; i < 3; i++)
    {
        Serial.print("Axis ");
        Serial.print(String(i+1));
        if (LIMITS[i])
        {
            Serial.println(" homed. Skipping axis.");
        }
        else {
            Serial.println(" not homed. Homing...");
            while (LIMITS[i] == false)
            {
                move(0, 0, 100);
            }
        }
    }

    while (1) {
        Serial.println("Delaying...");
        delay(1000);
    }
    Homing test code END
    REMOVE WHEN SERIAL REWRITE IS COMPLETED! REFERENCE ONLY!
    */
}

/*  return_axis_steps() - Returns a comma delimited report on the number of steps taken by the
                          axes. Presently up to the controller software to do the z-conversion.
                          TODO: Add z-displacement in real world units functionality.
*/
String return_axis_steps()
{
    String _tempstr;
    for (int i = 0; i < 3; i++)
    {
        _tempstr += AXES_COUNT[i];
        if (i != 2)
        {
            _tempstr += ",";
        }
    }
    return _tempstr;
}
/*  check_data() - Looks for inbound data on the serial port.
*/
void check_data()
{
    /*
        Serial Message Format:
        AXIS,DIRECTION,STEPS,EXTRADELAYUS
        N,N,NNNNNN,NNNNN\n\0
        length - 20 (incl null termination)
        n - number
        dont use windows line endings you dolt.
        \n only. UART will autoappend \0
    */
    if (Serial.available() > 10)
    {
        char terminator = '\n';
        String separator = ",";
        String incoming_data = Serial.readStringUntil(terminator);

        // split the first part of the message
        int _delimpos = 0;
        int _olddelimpos = 0;
        String _tempsubstr;
        _delimpos = incoming_data.indexOf(separator, _delimpos);
        // Assign the axis if it's valid
        int _axischeck = get_substring(_olddelimpos, _delimpos, incoming_data).toInt();
        if ((-1 < _axischeck) && (_axischeck < 4))
        {
            // valid axis
            REQUESTED_AXIS = _axischeck;
            if (REQUESTED_AXIS == 0)
            {
                home();
                return;
            }
        }
        else {
            Serial.println("BADMSG-AXIS");
            return;
        }
        // Split off the direction
        _olddelimpos = _delimpos + 1;
        _delimpos = incoming_data.indexOf(separator, _delimpos+1);
        int _dircheck = get_substring(_olddelimpos, _delimpos, incoming_data).toInt();
        if ((-1 < _dircheck) && (_dircheck < 2))
        {
            // valid direction request
            REQUESTED_DIR = _dircheck;
            Serial.println(get_substring(_olddelimpos, _delimpos, incoming_data));
        }
        else {
            // "is bullshit!" - I. Gharmarian
            Serial.println("BADMSG-DIRECTION");
            return;
        }
        // Split off the requested steps
        _olddelimpos = _delimpos + 1;
        _delimpos = incoming_data.indexOf(separator, _delimpos+1);
        int _stepcheck = get_substring(_olddelimpos, _delimpos, incoming_data).toInt();
        if ((-1 < _stepcheck) && (_stepcheck < 25000))
        {
            // Valid step request
            REQUESTED_STEPS = _stepcheck;
        }
        else {
            Serial.println("BADMSG-STEPVALUE");
            return;
        }

        // Split off the extra delay
        _olddelimpos = _delimpos + 1;
        _delimpos = incoming_data.indexOf(separator, _delimpos+1);
        int _delayus = get_substring(_olddelimpos, _delimpos, incoming_data).toInt();
        if (-1 < _delayus)
        {
            // valid delay
            REQUESTED_XTRA_US = _delayus;
        }
        else {
            Serial.println("BADMSG-XTRADELAY");
            return;
        }
        MOVE_REQUESTED = true;
        Serial.println("MSGOK");
        Serial.print("REQSTEPS: "); Serial.print(REQUESTED_STEPS);
        Serial.print(" REQDIR: "); Serial.print(REQUESTED_DIR);
        Serial.print(" REQAXIS: "); Serial.print(REQUESTED_AXIS);
        Serial.print(" REQEXTRADELAY: "); Serial.println(REQUESTED_XTRA_US);
    }
    return;
}

/*  get_substring(int begin_pos, int end_pos, String tempstring) -
        get_substring returns a substring of an Arduino String
        as an Arduino string. Use to split incoming messages along
        with strtok().
*/
String get_substring(int begin_pos, int end_pos, String tempstring)
{
    String return_string = "";

    for (int i = begin_pos; i < end_pos; i++)
    {
        return_string += tempstring[i];
    }
    return return_string;
}