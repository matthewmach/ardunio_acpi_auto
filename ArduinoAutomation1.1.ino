//----------------------------
// -   Arduino ACPI Automation      -
// -   By Matthew Mach              -
// -   Date: 06/23/2021             -
// -   Version 1.1                  -
// ----------------------------------
//
// This is the Arduino Code for ACPI Automation
// Refer to https://wiki.ith.intel.com/display/ITSvttsysengval/Arduino+User+Guide for usage details
//
// Features:
// This program will allow the Arduino to power on the system after a given amount of time
// By default, there are 4 types of tests
//      S5              - Delay of 30
//      Manual S3/S4    - Delay of 60
//      Combination     - Delay of 75 to allow the system to wake itself from sleep/hibernation
//      Custom          - Custom delay
//
// Each iteration in the loop take 200 milliseconds or 1/5th of the second.
// The program searches for user input every iteration (lower or upper case does not matter) and will respond with valid input
// It currently supports
//      pause           - Pauses the program
//      resume          - Resumes from a pauset2w
//      new             - Starts a new test
//      stop            - Stops the test
//      toggle          - Toggles the power
//
//
// Explanation:
// The program also reads the state of the - power led pin relative to the AREF and GND pins every iteration
// After 5 readings or 1 second, the program will take the average to determine if the system is on or off
// Usually, when the system is on, the pin reads a value close to 0. When the system is off, it usually reads a value of 1023.
// Milelage may vary depending on the motherboard.
// In this program, the variable pwrThreshold is the value compared to the average of the 5 readings
// If the average is under, the computer is detected as on, if over, it is detected as off.
//
// After determining the computer as off, the computer will wait the chosen delay then check twice again to ensure the system is off.
// If the system turns on during the delay, an error is thrown.
//
//
//
// Credit goes to ridencww (https://gist.github.com/ridencww/4e5d10097fee0b0f7f6b) for the serial_printf function
#include <stdio.h>
#include <ctype.h>

// --- Global Variables ---
//              |
// Variable     |  Description
// _____________|_______________________
// ## DEBUG ##  |
// debug        | Enables debug messages
bool debug = false;
String ver = "1.1";


// -- Ports --  |
// pwrLED       | Serial A0 -> input: Reads the status of the power LED
// pwrSwitch    | Digital 13 -> output: Outputs the signal to turn on and off the system
// readSpeed    | Reading speed of the pins, this is the number you put in PuTTY
// pwrThreshold | Expected max value when the system is on
int pwrLED = A0;
int pwrSwitch = 13;
int readSpeed = 9600;
int pwrThreshold = 100;


// -- Main --   |
// in           | Stores 5 readings at a time
// avg          | Average of the states
// inCount      | Stores the numbers of readings made since last check (index for state)
// cycle        | Stores current cycle
// startTime    | Stores the time the test started
// delayTime    | Stores the current delay
// state        | Indicates the current test running
//                States supported are
//                  - None          (no test, default)
//                  - S5            (S5 Shutdown test, turns on the system after 30 seconds)
//                  - Manual S3/S4  (S3/S4 manual power on, turns on the system after 60 seconds)
//                  - Combination   (Allows the system to wake itself and turns on the system from S5 after 75 seconds)
//                  - Custom        (Turns on the system after a user inputted amount of time)
// attemptOn    | Indicates whether the Arduino is attempting to power on the system after a delay
// paused       | Indicates whether the system is on or off
// pauseStart   | Time in which the pause began
// pauseTime    | Total amount of time paused
// powerStates  | Stores past 3 interupted power states
// powerCheck   | Counter for number of power checks before attempting to power on
int in[5] = {0, 0, 0, 0, 999};
int avg = 0;
int inCount = 0;
int cycle = 1;
long startTime = 0;
long delayTime = 0;
String state = "None";
bool attemptOn = false;
bool paused = false;
bool powerStates[3] = {false, false, false};
int powerCheck = 0;
bool newInput = false;



// --- Main Functions ---
// systemOn: void
// Runs what the system needs to run when the system is detected on
void systemOn() {
  // If the system was previously off, output that the system is detected on
  if (!powerStates[1])
    serial_printf(Serial, "System is on\r\n");
  // Check if the system powered on after attempting to turn it on
  if (attemptOn) {
    attemptOn = false;
    cycle++;
    serial_printf(Serial, "--- Cycle %d Start ---\r\n", cycle);
  }
  // If the system is attemping to power on and going through the checking process
  if (powerCheck > 0) {
    powerCheck++;
    // If the Arduino was on for both checks after the delay, throw an error
    if (powerCheck == 2 && (powerStates[0] || powerStates[1])) {
      serial_printf(Serial, "!!! ERROR !!! System powered on before Arduino power on\r\n");
      powerCheck = 0;
    }
  }
}


// systemOff: void
// Runs what the system needs to run when the system is detected off
void systemOff() {
  // If the system was previously on, output that the system is detected off
  if (powerStates[1])
    serial_printf(Serial, "System is off\r\n");
  // Check if the system didn't power on after attempting to turn it on
  if (attemptOn) {
    serial_printf(Serial, "!!! ERROR !!! System did not power on properly\r\n");
    attemptOn = false;
  }
  // If there is a test or the system is not paused, continue with attempt to power on
  if (!(state.compareTo("None") == 0 || state.compareTo("Paused") == 0)) {
    // Check to see if the power is off a total of 3 times before powering on
    if (powerCheck == 2) {
      // Power the system on
      powerToggle();
      powerCheck = 0;
      // Indicate that the system is attempting to turn on
      attemptOn = true;
    }
    else {
      // If the powerCheck is just beginning then delay the system
      if (powerCheck == 0) {
        serial_printf(Serial, "Power On in %d seconds\r\n", delayTime / 1000);
        delay(delayTime - 2000);
      }
      // Increment the powercheck
      powerCheck++;
    }
  }
}

// powerToggle: void
// Powers on the motherboard
// Works by shorting the signal between the power switch pins
void powerToggle() {
  serial_printf(Serial, "Attempting power toggle\r\n");
  // Shorts the switch by sending a low signal
  digitalWrite(pwrSwitch, LOW);
  delay(1000);
  // Turn off the short
  digitalWrite(pwrSwitch, HIGH);
  // Wait 5 seconds for system to boot (more a of precaution than actually needed)
  delay(4800);
}


// readSerialInput: void
// Reads the serial input
String readSerialInput() {
  String inp = "";
  // endMarker is the character for the end of a line
  // PuTTY enters Carriage Return
  char endMarker = '\r', c;
  // While there is availible data and the data hasn't met the end character
  while (Serial.available() > 0 && !newInput) {
    c = Serial.read();
    // If the chracter isn't the endMarker, add it to the input string
    if (c != endMarker) {
      inp += c;
    }
    else
      newInput = true;
    // Add delay to wait for next character in input, as input comes in slowly relative to the calculation speed of the Arduino
    delay(100);
  }
  return inp;
}



// checkSerialInput: void
// Checks for, reads and handles serial input accordingly
void checkSerialInput() {
  String serialInput = readSerialInput();
  serialInput.toLowerCase();
  if (newInput) {
    // Input "pause" case
    // Pauses the program and records the time of pause
    if (serialInput.compareTo("pause") == 0) {
      serial_printf(Serial, "%s Testing has been paused\r\n", state);
      paused = true;
    }
    // Input "resume" case
    // Resumes the program and records the amount of time paused and removes it from the test time
    else if (serialInput.compareTo("resume") == 0) {
      serial_printf(Serial, "%s Testing is resuming\r\n", state);
      paused = false;
    }
    // Input "new" case
    // Starts a new test: S5, S3/S4, Combination, or Custom
    // Resets all variables and sets proper delay
    else if (serialInput.compareTo("new") == 0) {
      bool correctInput = false;
      // Ask user for input
      while (!correctInput) {
        serialInput = "";
        newInput = false;
        Serial.println("Please choose what test you want to run:");
        Serial.println("    1) S5");
        Serial.println("    2) Manual S3/4");
        Serial.println("    3) Combination");
        Serial.println("    4) Custom");
        Serial.print("Your Input: ");
        while (!newInput) {
          serialInput = readSerialInput();
        }
        if (serialInput.compareTo("1") == 0 || serialInput.compareTo("2") == 0 || serialInput.compareTo("3") == 0 || serialInput.compareTo("4") == 0)
          correctInput = true;
        else
          Serial.println("Please input a proper choice \r\n");
      }
      // Reset all variables
      for (int x = 0; x < 4; x++)
        in[x] = 0;
      in[4] = 999;
      cycle = 1;
      inCount = 0;
      startTime = millis();
      paused = false;
      for (int x = 0; x < 3; x++)
        powerStates[x] = false;
      powerCheck = 0;
      // Set proper delay and state
      if (serialInput.compareTo("1") == 0) {
        delayTime = 30000;
        state = "S5";
        serial_printf(Serial, "Commencing S5 Test with power on delay of %d\r\n", delayTime / 1000  );
      }
      else if (serialInput.compareTo("2") == 0) {
        delayTime = 60000;
        state = "Manual S3/S4";
        serial_printf(Serial, "Commencing Manual S3/S4 Test with power on delay of %d\r\n", delayTime / 1000);
      }
      else if (serialInput.compareTo("3") == 0) {
        delayTime = 75000;
        state = "Combination";
        serial_printf(Serial, "Commencing Combination Test with power on delay of %d\r\n", delayTime / 1000);
      }
      else if (serialInput.compareTo("4") == 0) {
        // Ask for input for custom delay
        delayTime = 0;
        while (delayTime == 0) {
          serialInput = "";
          newInput = false;
          Serial.print("Please input the delay you want (cannot be 0): ");
          while (serialInput == "" && !newInput) {
            serialInput = readSerialInput();
          }
          delayTime = serialInput.toInt();
          delayTime *= 1000;
        }
        state = "Custom";
        serial_printf(Serial, "Commencing Custom Test with power on delay of %d seconds\r\n", delayTime / 1000);
      }
      serial_printf(Serial, "--- Cycle %d Start ---\r\n", cycle);
    }
    // Input "stop" case
    // Stops a test
    else if (serialInput.compareTo("stop") == 0) {
      serial_printf(Serial, "Stopping %s test\r\n", state);
      state = "None";
    }
    // Input "toggle" case
    // Manual power toggle for the system
    else if (serialInput.compareTo("toggle") == 0) {
      powerToggle();
    }
    else if (serialInput.compareTo("debug") == 0) {
      printDebug();
    }
    newInput = false;
  }
}



// --- Startup ---
// Sets up all the pins to begin reading
// NOTE: Pins will always default to LOW at Arduino Boot. This WILL shutdown/turn on the system after booting the Arduino!!!!
void setup() {
  // Begin reading pins and specify speed
  Serial.begin(readSpeed);
  // Set pin input and output
  pinMode(pwrLED, INPUT);
  pinMode(pwrSwitch, OUTPUT);
  // Sets analog reference to the AREF pin, this stabilizes input
  analogReference(EXTERNAL);
  // Set the output of pwrSwitch to HIGH
  digitalWrite(pwrSwitch, HIGH);
  // Startup Output
  serial_printf(Serial, "---------- Arduino Automation ----------\r\n");
  serial_printf(Serial, "### Version: %s \r\n", ver);
}



// --- Loop ---
void loop() {
  // Check for manual user input
  checkSerialInput();
  // Read current input from the motherboard power LED
  in[inCount] = analogRead(pwrLED);
  // Once 5 readings have been made, check if the system is on or off (index starts at 0)
  if (inCount == 4) {
    // Takes the average of the input
    avg = (in[0] + in[1] + in[2] + in[3] + in[4]) / 5;
    updatePowerStates(avg < pwrThreshold);
    // If debugging is turned on, print all global variables
    if (debug)
      printDebug();
    // Check if the system is on
    if (powerStates[0])
      systemOn();
    // If not on, then it's off
    else
      systemOff();
    // Reset input counter
    inCount = 0;
  }
  else
    inCount++;
  // Delay next reading by 1/5th of a second
  delay(200);
}



// --- Helper Functions ---
// updatePowerStates: void
// Updates the powerStates
void updatePowerStates(bool on) {
  powerStates[2] = powerStates[1];
  powerStates[1] = powerStates[0];
  powerStates[0] = on;
}



// --- Pretty Printing Functions ---
// printDebug: void
// Prints debug messages
void printDebug() {
  serial_printf(Serial, "### DEBUG ### States: %d, %d, %d, %d, %d \r\n", in[0], in[1], in[2], in[3], in[4]);
  serial_printf(Serial, "### DEBUG ### Average: %d \r\n", avg);
  serial_printf(Serial, "### DEBUG ### State: %s   Delay: %d \r\n", state, delayTime);
  serial_printf(Serial, "### DEBUG ### powerCheck: %d   attemptOn: %o \r\n", powerCheck, attemptOn);
  serial_printf(Serial, "### DEBUG ### Paused?: %o   startTime: %d \r\n", paused, startTime);
}

// printPad: String -> String
// Pads number with zeroes for time display
String printPad (String input) {
  if (input.length() < 2)
    return "0" + input;
  return input;
}

// printTime: void -> String
// Return a time stamp based on the time elapsed
String printTime() {
  // Calculate the time
  long s = (millis() - startTime) / 1000, m = s / 60, h = m / 60;
  // Convert values to strings
  char se[7], mi[7], ho[7];
  itoa(h, ho, 10);
  itoa(m % 60, mi, 10);
  itoa(s % 60, se, 10);
  return "[" + printPad(ho) + ":" + printPad(mi) + ":" + printPad(se) + "] ";
}


// serial_printf: void
// Printf function for Arduino with timestamp
// Credit goes to ridencww (https://gist.github.com/ridencww/4e5d10097fee0b0f7f6b)
void serial_printf(HardwareSerial& serial, const char* fmt, ...) {
  if (state.compareTo("None") != 0)
    serial.print (printTime());
  va_list argv;
  va_start(argv, fmt);

  for (int i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] == '%') {
      // Look for specification of number of decimal places
      int places = 2;
      if (fmt[i + 1] == '.') i++; // alw1746: Allows %.4f precision like in stdio printf (%4f will still work).
      if (fmt[i + 1] >= '0' && fmt[i + 1] <= '9') {
        places = fmt[i + 1] - '0';
        i++;
      }

      switch (fmt[++i]) {
        case 'B':
          serial.print("0b"); // Fall through intended
        case 'b':
          serial.print(va_arg(argv, int), BIN);
          break;
        case 'c':
          serial.print((char) va_arg(argv, int));
          break;
        case 'd':
        case 'i':
          serial.print(va_arg(argv, int), DEC);
          break;
        case 'f':
          serial.print(va_arg(argv, double), places);
          break;
        case 'l':
          serial.print(va_arg(argv, long), DEC);
          break;
        case 'o':
          serial.print(va_arg(argv, int) == 0 ? "off" : "on");
          break;
        case 's':
          serial.print(va_arg(argv, String));
          break;
        case 'X':
          serial.print("0x"); // Fall through intended
        case 'x':
          serial.print(va_arg(argv, int), HEX);
          break;
        case '%':
          serial.print(fmt[i]);
          break;
        default:
          serial.print("?");
          break;
      }
    } else {
      serial.print(fmt[i]);
    }
  }
  va_end(argv);
}
