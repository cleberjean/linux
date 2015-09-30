/*******************************************************************************
   readbmp - Reading the Temperature and Pressure from a Bosch BMP085 on Linux
   ===========================================================================
   Version 1.1 (26-Sep-2015) - Cleber Jean Barranco (cleberjean@hotmail.com)

   This code will read the temperature and pressure from BMP085 connected on
   the VGA port (pins 5 = GND, 9 = +5 Vcc, 12 = SDA and 15 = SCL) to every 1
   second and display them on the command line until that any key is pressed.

       VGA Female Front Side View (holes)
       ----------------------------------
 
              -   4   3   2   1

               10   +   8   7   6

              L  14  13   A  11


   Where:
          - = Gnd
          + = +5Vcc
          L = SDL
          A = SDA


   Based on original Jesse Brannon's code for Raspberry PI in:
   https://github.com/brannojs/ECE497/blob/master/MiniProject02/pressurei2c.c

   * Uses ANSI Escape Sequences to print colorful characters on screen.

   Compile it with "$ gcc -o readbmp readbmp.c -lm"
   or "sudo auto-apt run gcc -o readbmp readbmp.c -lm"

   Others Referencies:
       https://github.com/brannojs/ECE497/blob/master/MiniProject02/pressurei2c.c
       http://www.thegeekstuff.com/2013/01/c-argc-argv/
       http://stackoverflow.com/questions/8626939/is-there-is-any-alternatives-to-cprintf
       http://cboard.cprogramming.com/c-programming/63166-kbhit-linux.html
       http://forums.fedoraforum.org/showthread.php?t=270335
       http://stackoverflow.com/questions/3596310/c-how-to-use-the-function-uname
       http://courses.cms.caltech.edu/cs11/material/c/mike/misc/cmdline_args.html
       http://bitmote.com/index.php?post/2012/11/19/Using-ANSI-Color-Codes-to-Colorize-Your-Bash-Prompt-on-Linux
       https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
       http://www.codingunit.com/c-reference-stdlib-h-function-getenv
       http://stackoverflow.com/questions/9753346/determine-if-a-c-string-is-a-valid-int-in-c
       http://www.csl.mtu.edu/cs4411.choi/www/Resource/signal.pdf
       https://en.wikipedia.org/wiki/Unix_signal
       https://en.wikipedia.org/wiki/C_signal_handling
       http://en.cppreference.com/w/c/program/signal
       http://www.gnu.org/software/libc/manual/html_node/Termination-in-Handler.html#Termination-in-Handler

   I hope which this code will be util.

   Stuffs still to do:
       Optimize code and functions.

   P.S.: I don't know if the explanations are clear because I don't speak english.

*******************************************************************************/

/*** Libraries declarations ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
//#include <poll.h>           // ?
#include <math.h>           // to calculate sea level pressure (pow() function)
#include <signal.h>         // to handle CTRL+C and kill command
#include <termios.h>        // to perform "no echo" from keyboard into terminal
//#include <linux/version.h>  // to check kernel version (not yet implemented)
//#include <sys/utsname.h>    // to check kernel version (not yet implemented)

/*** Libraries declarations end ***/


#define MAX_BUF 64          // code's original constant (not changed)


// Define bool type in standard C (C++ don't needs)
#ifndef __cplusplus
enum boolean {true = 1, false = 0};
typedef enum boolean bool;  // now has bool type on standard C
#endif


/*** Global vars (useds by several functions) ***/

bool ctrlc_press = false;   // informs if CRTL+C was pressed to the program loop
int i2cbus = 1;             // default i2c bus number

// Keyboard echo control
struct termios org_term, new_term;
int oldf;

/*** Global vars end ***/


/*** Escape Sequences - Foreground Colours ***/
/* See details in https://en.wikipedia.org/wiki/ANSI_escape_code#Colors or
   (better) http://bitmote.com/index.php?post/2012/11/19/Using-ANSI-Color-Codes-to-Colorize-Your-Bash-Prompt-on-Linux
*/

#define BLACK           "\033[0;30m"
#define RED             "\033[0;31m"
#define GREEN           "\033[0;32m"
#define YELLOW          "\033[0;33m"
#define BLUE            "\033[0;34m"
#define MAGENTA         "\033[0;35m"
#define CYAN            "\033[0;36m"
#define WHITE           "\033[0;37m"
#define BRIGHT_BLACK    "\033[1;30m"
#define BRIGHT_RED      "\033[1;31m"
#define BRIGHT_GREEN    "\033[1;32m"
#define BRIGHT_YELLOW   "\033[1;33m"
#define BRIGHT_BLUE     "\033[1;34m"
#define BRIGHT_MAGENTA  "\033[1;35m"
#define BRIGHT_CYAN     "\033[1;36m"
#define BRIGHT_WHITE    "\033[1;37m"

#define RST_SCRN_ATTS   "\033[m"      // resets screen colours attributes

/*** Escape sequences end ***/


/*** Functions prototypes ***/

void show_help(char *prog_name);
void show_info(char *prog_name);
bool isroot(void);
bool key_pressed(void);
float getSLP(int Altitude, float absPressure);
bool close_bmp085();
void signal_handler(int sig);
void disable_echo(void);
void restore_echo(void);

/*** Prototypes functions end ***/


int main(int argc, char **argv) {
	int LocalAltitude = 9999;   // local altitude to compute sea level pressure
	bool noloop = false;        // program loop flag

	printf("\n");  // jumps 1 row (duh!)

	// Checks command line options
	if (argc > 1) {
		bool syntax_ok = false;

		int i;  // arguments counter

		for (i = 1; i < argc; i++) {
			// Checks if -alt option is present
			if (strcmp(argv[i], "-alt") == 0) {
				if (argv[++i] != NULL) {
					char str_value[5];
					LocalAltitude = atoi(argv[i]);
					snprintf(str_value, sizeof str_value, "%d", LocalAltitude);
					if ((strcmp(argv[i], str_value) != 0) || (LocalAltitude < -500) || (LocalAltitude > 9000)) {
						printf("Bad altitude value: %s meter(s)\n\n", argv[i]);
						return 1;
					}
					syntax_ok = true;
				} else {
					printf("Option '-alt' without altitude!\n\n");
					return 1;
				}  // if (argv[++i] != NULL) {...} else {...}
			}      // if (strcmp(argv[i], "-alt") == 0) {...}

			// Checks if -i2c option is present
			if (strcmp(argv[i], "-i2c") == 0) {
				if (argv[++i] != NULL) {
					char str_value[5];
					i2cbus = atoi(argv[i]);
					snprintf(str_value, sizeof str_value, "%d", i2cbus);
					if ((strcmp(argv[i], str_value) != 0) || (i2cbus < 0) || (i2cbus > 9)) {
						printf("Bad I2C bus number: %s\n\n", argv[i]);
						return 1;
					}
					syntax_ok = true;
				} else {
					printf("Option '-i2c' without bus number!\n\n");
					return 1;
				}  // if (argv[++i] != NULL) {...} else {...}
			}      // if (strcmp(argv[i], "-i2c") == 0) {...}

			// Checks if -nl or --noloop option is present
			if ((strcmp(argv[i], "-nl") == 0) || (strcmp(argv[i], "--noloop") == 0)) {
				noloop = true;
				syntax_ok = true;
			}

			// Checks if -h or --help option is present
			if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
				show_info(argv[0]);
				return 0;
			}

			// Bad syntax
			if (!syntax_ok) {
				printf("Unavailable option: ");
				printf(BRIGHT_RED);    // error message written in red
				printf("%s\n", argv[1]);
				printf(RST_SCRN_ATTS); // backs to original screen attributes
				show_help(argv[0]);
				return 1;
			}
		}      // for (i = 1; i < argc; i++) {...}
	}          // if (argc > 1) {...}

	// User is root?
	if (!isroot()) {
		printf("You must be root to run this program.\n\n");
		return 1;
	}

	// Traps CTRL+C, kill command
	signal(SIGINT, signal_handler);  // call signal_handler function if CTRL+C/CTRL+D is pressed
	signal(SIGTERM, signal_handler); // call signal_handler function if kill, killall commands are invocateds (except kill -9 which not be intercepted)

	disable_echo();

	FILE *fp;
	char path[MAX_BUF];
	bool temp_ok, press_ok;  // status flags to temperature and pressure

/*	// http://stackoverflow.com/questions/3596310/c-how-to-use-the-function-uname
	struct utsname unameData;
	if (uname(&unameData)) {
		printf("%c[%d;%dm", ESC, BRIGHT, RED + 30);  // error message written in red
		printf("\n\aNao foi possível identificar a versão do Linux!\n\n");
		printf("%c[%dm", ESC, RESET);  // backs to original screen attributes
		return EXIT_FAILURE;
	}
*/

	// Checks if the device already is opened 
	snprintf(path, sizeof path, "/sys/bus/i2c/drivers/bmp085/%d-0077/temp0_input", i2cbus);
	if ((fp = fopen(path, "r")) == NULL) {
		//Open a file to I2C Bus and tell it that there is a BMP085 located at 0x77.
		snprintf(path, sizeof path, "/sys/class/i2c-adapter/i2c-%d/new_device", i2cbus);
		if ((fp = fopen(path, "w")) == NULL) {
			printf(BRIGHT_RED);    // error message written in red
			printf("\aDevice opening failed!\n\n");
			printf(RST_SCRN_ATTS); // backs to original screen colors attributes
			restore_echo();        // restores keyboard echo
			return 1;
		}
	}
	rewind(fp);
	fprintf(fp, "bmp085 0x77\n");
	fflush(fp);
	fclose(fp);

	// Program loop
	do {
		char buf[MAX_BUF];

		// Attempts to open the file of the device.
		snprintf(path, sizeof path, "/sys/bus/i2c/drivers/bmp085/%d-0077/temp0_input", i2cbus);
		if ((fp = fopen(path, "r")) == NULL) {
			if (close_bmp085()) {
				printf(BRIGHT_RED);    // error message written in red
				printf("\aCouldn't open temperature file or file not found! BMP085 is installed?\n\n");
				printf(RST_SCRN_ATTS); // backs to original screen colors attributes
			}
			restore_echo();  // restores keyboard echo
			return 1;
		}

		// Attempts to read the temperature from the device.
		temp_ok = true;
		if (fgets(buf, MAX_BUF, fp) == NULL) {
			printf(BRIGHT_RED);  // error message written in red
			printf("Couldn't read the temperature! ->");
			temp_ok = false;
		}
		fclose(fp);
		float temp = atoi(buf);
		float tempd = temp / 10.0;
		if (temp_ok) printf(WHITE);
		printf(" Current Temperature : ");
		if (temp_ok) printf(BRIGHT_CYAN);
		printf("%.1f °C\n", tempd);

		// Attempts to open the file of the device.
		snprintf(path, sizeof path, "/sys/bus/i2c/drivers/bmp085/%d-0077/pressure0_input", i2cbus);
		if ((fp = fopen(path, "r")) == NULL) {
			if (close_bmp085()) {
				printf(BRIGHT_RED);    // error message written in red
				printf("\aCouldn't open pressure file or file not found! BMP085 is installed?\n\n");
				printf(RST_SCRN_ATTS); // backs to original screen colors attributes
			}
			restore_echo();  // restores keyboard echo
			return 1;
		}

		// Attempts to read the pressure from the device.
		press_ok = true;
		if (fgets(buf, MAX_BUF, fp) == NULL) {
			printf(BRIGHT_RED);  // error message written in red
			printf("Couldn't read the pressure! ->");
			press_ok = false;
		}
		fclose(fp);
		float pressure = atoi(buf);
		if (press_ok) printf(WHITE);
			printf(" Absolute Pressure   : ");
		if (press_ok) printf(BRIGHT_CYAN);
			printf("%.1f hPa\n", pressure / 100.0f);
		if (press_ok && (LocalAltitude <= 9000)) {
			printf(WHITE);
			printf(" Sea Level Pressure  : ");
			printf(BRIGHT_CYAN);
			printf("%.1f hPa\n", getSLP(LocalAltitude, pressure) / 100.0f);
		}
		sleep(1);
		printf("\n");
	} while (!key_pressed() && !noloop && !ctrlc_press);

	restore_echo();  // restores keyboard echo

	//Close file of BMP085 located at I2C bus
	printf(BRIGHT_WHITE);
	printf("Disconnecting BMP085 (0x77) from I2C bus... ");
	if (!close_bmp085()) return 1;
	printf("Ok.\n\n");
	printf(RST_SCRN_ATTS);  // backs to original screen colors attributes
	return 0;
}  // main function end


/*****************************************
  Help Information (only in syntax error)
 *****************************************/
void show_help(char *prog_name) {
	printf("\n  Usage: ");
	printf(BRIGHT_WHITE);
	printf("%s [-i2c bus_#] [-alt altitude] [-nl | --noloop] [-h | --help]", prog_name);
	printf(RST_SCRN_ATTS);  // backs to original screen attributes
	printf("\n\nWhere: bus_# = I2C bus number (0 to 9) where BMP085 is installed.\n");
	printf("               (If none I2C bus is specified, the I2C bus 1 will be used).\n");
	printf("    altitude = local altitude in meters (-500 to 9000).\n\n");
}


/***************************************
  Full Help Information (--help option)
 ***************************************/
void show_info(char *prog_name) {
	printf(BRIGHT_WHITE);
	printf("readbmp");
	printf(RST_SCRN_ATTS);  // backs to original screen attributes
	printf(" - Reading Temperature and Pressure from a Bosch BMP085 on Linux\n");
	printf("=========================================================================\n");
	printf("Ver. 1.1 (26-Sep-2015) - Cleber Jean Barranco (cleberjean@hotmail.com)\n");
	show_help(prog_name);
}


/************************
  Checks if user is root
 ************************/
bool isroot(void) {
	char *ptr_user;
	ptr_user = getenv("USER");
	if (ptr_user != NULL) {
		if (strcmp(ptr_user, "root") == 0) return true; else return false;
	} else {
		printf("Environment Variable 'USER' not found.\n");
		return false;
	}
}


/*******************************
  Checks if any key was pressed
 *******************************/
bool key_pressed(void) {
	if (getchar() != EOF) return true;
	return false;
}


/*****************************************************************************
  Gets the Sea Level Pressure (p0) at a determinated altitude (in meters) and
  pressure (in Pascal).
 *****************************************************************************/
float getSLP(int Altitude, float absPressure) {
	return (absPressure / pow(1.0f - (Altitude / 44330.0f), 5.255f));  // sugerido pelo fabricante
}


/**************************************************************
  Release BMP from bus and return true if ok else return false
 **************************************************************/
bool close_bmp085() {
	FILE *fp;
	char path[MAX_BUF];

	snprintf(path, sizeof path, "/sys/class/i2c-adapter/i2c-%d/delete_device", i2cbus);
	if ((fp = fopen(path, "w")) == NULL) {
		printf(BRIGHT_RED);    // error message written in red
		printf("\n\aCouldn't disconnect the device from I2C bus!\n\n");
		printf(RST_SCRN_ATTS); // backs to original screen attributes
		return false;          // fail on device disconnection
	}
	rewind(fp);
	fprintf(fp, "0x77\n");
	fflush(fp);
	fclose(fp);

	return true;               // device disconnected successfully
}


/*****************************************************
  Handles Control Signals (CTRL+C, kill, killall,...)
 *****************************************************/
void signal_handler(int sig) {
	if (sig == SIGTERM) {
		printf(BRIGHT_YELLOW);     // error message written in yellow
		printf("\nKill signal received!\n");
		printf(BRIGHT_WHITE);      // error message written in white
		printf("\nTrying to finish pending actions... ");
		restore_echo();            // restores keyboard echo
		if (close_bmp085()) {
			printf("Ok.\n");
			printf(RST_SCRN_ATTS); // original screen colors attributes
			printf("\n");          // backs to original screen colors attributes (here, don't backs without it and I don't know why!)
		}

		/* http://www.gnu.org/software/libc/manual/html_node/Termination-in-Handler.html#Termination-in-Handler
		   "Now reraise the signal. We reactivate the signal’s default handling,
		    which is to terminate the process.
		    We could just call exit or abort, but reraising the signal sets the
		    return status from the process correctly."
		*/
		signal(sig, SIG_DFL);
		raise(sig);
	}  // kill end
		
	if (sig == SIGINT) {
//		signal(sig, SIG_IGN); // suggestion from http://www.csl.mtu.edu/cs4411.choi/www/Resource/signal.pdf
		ctrlc_press = true;
	}
}


/**********************************
  Disable terminal's keyboard echo
 **********************************/
void disable_echo(void) {
	tcgetattr(STDIN_FILENO, &org_term);           // gets original terminal attributes
	new_term = org_term;                          // copies to new attributes struct 
	new_term.c_lflag &= ~(ICANON | ECHO);	      // clear ICANON and ECHO
	tcsetattr(STDIN_FILENO, TCSANOW, &new_term);  // sets the new terminal attributes
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
}


/***********************************
  Restores terminal's keyboard echo
 ***********************************/
void restore_echo(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &org_term);
	fcntl(STDIN_FILENO, F_SETFL, oldf);
}
