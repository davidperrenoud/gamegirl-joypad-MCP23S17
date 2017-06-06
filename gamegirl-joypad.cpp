#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <wiringPi.h>
#include <iostream>
#include <mcp23s17.h>

#define die(str, args...) do { \
    perror(str); \
    exit(EXIT_FAILURE); \
  } while(0)

#define BASE    100 // offset for GPIO expander numbering

using namespace std;

int _SHDN = 25; // GPIO25 - Power switch

/* MCP23S17 PINOUT   Pin 1(GPB0) start at GPIO 100 GPB0 = 100 | GPA0 = 108
 *   LBO | 1 (100) - GPB0     GPA7 - (115) 28 |  R1
 *   SS1 | 2 (101) - GPB1     GPA6 - (114) 27 |  Select
 *  WiFi | 3 (102) - GPB2     GPA5 - (113) 26 |  Start
 *  Left | 4 (103) - GPB3     GPA4 - (112) 25 |  Y (West)
 *    Up | 5 (104) - GPB4     GPA3 - (111) 24 |  X (North)
 * Right | 6 (105) - GPB5     GPA2 - (110) 23 |  A (East)
 *  Down | 7 (106) - GPB6     GPA1 - (109) 22 |  B (South)
 *    L1 | 8 (107) - GPB7     GPA0 - (108) 21 |  AMP 
 *   3V3 | 9       - VDD      INTA -       20 |  NC
 *   GND | 10      - VSS      INTB -       19 |  NC
 *   GND | 11      - CS        RST -       18 |  3V3
 *   SCK | 12      - SCK        A2 -       17 |  GND
 *  MOSI | 13      - SI         A1 -       16 |  GND
 *  MISO | 14      - SO         A0 -       15 |  GND    
 */

int btn_left = 103;
int btn_up = 104;
int btn_right = 105;
int btn_down = 106;
int btn_l1 = 107;
int btn_b = 109;
int btn_a = 110;
int btn_x = 111;
int btn_y = 112;
int btn_start = 113;
int btn_selec = 114;
int btn_r1 = 115;

int _LBO = 100; //Critical Low Battery alert, set as input pulled high
int _SS1 = 101; //SPI Chip Select pin for LCD, pull high
int _WiFi_EN = 102; //Pull high to enable
int _AMP_EN = 108; //Pull high to enable
bool WiFi_status = 1; //Sets to on by default
bool AMP_status = 1; //Sets to on by default

void set_key_bit(int fd, int button) {
  if (ioctl(fd, UI_SET_KEYBIT, button) < 0) {
    die("error: ioctl");
  }
}

void set_button_event(int fd, int button, int value) {
  struct input_event ev;
  ev.type = EV_KEY;
  ev.code = button;
  ev.value = value;
  if (write(fd, &ev, sizeof(struct input_event)) < 0) {
    die("error: write");
  }
}

int main() {
  unsigned i;

  // Initialize GPIO
  wiringPiSetupGpio();
  
  // Initialize MCP23S17
  mcp23s17Setup (BASE, 0, 0) ;
  
  //Set all button input pins on MCP23S17 to Input with PullUps enabled
  for (i = 2 ; i < 7 ; ++i){ //102 - 107
    pinMode (BASE+i, INPUT);
    pullUpDnControl(BASE+i,PUD_UP);
  }
  for (i = 9 ; i < 15 ; ++i){ //109-115
    pinMode (BASE+i, INPUT);
    pullUpDnControl(BASE+i,PUD_UP);
  }

  //Power switch setup
  pinMode(_SHDN, INPUT);
  pullUpDnControl(_SHDN,PUD_UP);
  
  //Sets Chip Select pin of LCD high disabling the SPI connection
  pinMode (_SS1,OUTPUT);
  digitalWrite(_SS1,1);

  //Enables WiFi by default
  pinMode (_WiFi_EN,OUTPUT);
  digitalWrite(_WiFi_EN,1);
    
  //Sets up Audio, already on by default
  pinMode (_AMP_EN,OUTPUT);
  digitalWrite(_AMP_EN,1);
  
  // Initialise udev
  struct uinput_user_dev uidev;
  struct input_event ev;

  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    die("error: open");

  if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
    die("error: ioctl");
  if (ioctl(fd, UI_SET_EVBIT, EV_REL) < 0)
    die("error: ioctl");
  if (ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0)
    die("error: ioctl");

  set_key_bit(fd, BTN_DPAD_UP);
  set_key_bit(fd, BTN_DPAD_DOWN);
  set_key_bit(fd, BTN_DPAD_LEFT);
  set_key_bit(fd, BTN_DPAD_RIGHT);
  set_key_bit(fd, BTN_NORTH);
  set_key_bit(fd, BTN_SOUTH);
  set_key_bit(fd, BTN_WEST);
  set_key_bit(fd, BTN_EAST);
  set_key_bit(fd, BTN_START);
  set_key_bit(fd, BTN_SELECT);
  set_key_bit(fd, BTN_TL);
  set_key_bit(fd, BTN_TR);

  ioctl(fd, UI_SET_EVBIT, EV_ABS);
  ioctl(fd, UI_SET_ABSBIT, ABS_X);
  ioctl(fd, UI_SET_ABSBIT, ABS_Y);
  uidev.absmin[ABS_X] = 0;
  uidev.absmax[ABS_X] = 4;
  uidev.absmin[ABS_Y] = 0;
  uidev.absmax[ABS_Y] = 4;

  memset(&uidev, 0, sizeof(uidev));
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Gamegirl Controller");
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor  = 1;
  uidev.id.product = 1;
  uidev.id.version = 1;

  if (write(fd, &uidev, sizeof(uidev)) < 0)
    die("error: write");

  if (ioctl(fd, UI_DEV_CREATE) < 0)
    die("error: ioctl");

  int up_button, up_button_old         = 1;
  int down_button, down_button_old     = 1;
  int left_button, left_button_old     = 1;
  int right_button, right_button_old   = 1;
  int x_button, x_button_old           = 1;
  int b_button, b_button_old           = 1;
  int y_button, y_button_old           = 1;
  int a_button, a_button_old           = 1;
  int start_button, start_button_old   = 1;
  int selec_button, selec_button_old   = 1;
  int l1_button, l1_button_old         = 1;
  int r1_button, r1_button_old         = 1;
  int powerswitch                      = 1;
  int LBO                              = 1;
  int toggle_timeout                   = 0; //Toggle timeout for toggling WiFi or Amp

  while (1) {
    // Read GPIO
    up_button_old     = up_button;
    down_button_old   = down_button;
    left_button_old   = left_button;
    right_button_old  = right_button;
    x_button_old      = x_button;
    b_button_old      = b_button;
    y_button_old      = y_button;
    a_button_old      = a_button;
    start_button_old  = start_button;
    selec_button_old  = selec_button;
    l1_button_old     = l1_button;
    r1_button_old     = r1_button;

    up_button    = digitalRead(btn_up); // 
    down_button  = digitalRead(btn_down); // 
    left_button  = digitalRead(btn_left); // 
    right_button = digitalRead(btn_right); // 
    x_button     = digitalRead(btn_x); //
    b_button     = digitalRead(btn_b); // 
    y_button     = digitalRead(btn_y); // 
    a_button     = digitalRead(btn_a); //
    start_button = digitalRead(btn_start); //
    selec_button = digitalRead(btn_selec); // 
    l1_button    = digitalRead(btn_l1); // 
    r1_button    = digitalRead(btn_r1); // 
    powerswitch  = digitalRead(_SHDN); //
    LBO          = digitalRead(_LBO); //

    // Write to udev
    if (up_button != up_button_old) {
      set_button_event(fd, BTN_DPAD_UP, up_button != 0);
    }

    if (down_button != down_button_old) {
      set_button_event(fd, BTN_DPAD_DOWN, down_button != 0);
    }

    if (left_button != left_button_old) {
      set_button_event(fd, BTN_DPAD_LEFT, left_button != 0);
    }

    if (right_button != right_button_old) {
      set_button_event(fd, BTN_DPAD_RIGHT, right_button != 0);
    }

    if (x_button != x_button_old) {
      set_button_event(fd, BTN_NORTH, x_button != 0);
    }

    if (b_button != b_button_old) {
      set_button_event(fd, BTN_SOUTH, b_button != 0);
    }

    if (y_button != y_button_old) {
      set_button_event(fd, BTN_WEST, y_button != 0);
    }

    if (a_button != a_button_old) {
      set_button_event(fd, BTN_EAST, a_button != 0);
    }

    if (start_button != start_button_old) {
      set_button_event(fd, BTN_START, start_button != 0);
    }

    if (selec_button != selec_button_old) { //mode button | Sel+L1 WiFi toggle | Sel+R1 = Amp toggle | 
      if (l1_button == 0 && toggle_timeout > 119){
        WiFi_status = !WiFi_status;
        digitalWrite(_WiFi_EN,WiFi_status);
        toggle_timeout = 0;
      }
      else if(r1_button == 0 && toggle_timeout > 119){
        AMP_status = !AMP_status;
        digitalWrite(_AMP_EN,AMP_status);
        toggle_timeout = 0;
      }
      else
        set_button_event(fd, BTN_SELECT, selec_button != 0);
    }

    if (l1_button != l1_button_old) {
      set_button_event(fd, BTN_TL, l1_button != 0);
    }

    if (r1_button != r1_button_old) {
      set_button_event(fd, BTN_TR, r1_button != 0);
    }

    if (LBO == 0) {
      system("Critically low battery shutting down now!\n");
      system("echo -n \"SHUTDOWN\" | nc -u -w1 127.0.0.1 55355");
    }

    if (powerswitch == 0) {
      system("Power button toggled, shutting down now!\n");
      system("echo -n \"SHUTDOWN\" | nc -u -w1 127.0.0.1 55355");
    }

    memset(&ev, 0, sizeof(struct input_event));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(fd, &ev, sizeof(struct input_event)) < 0)
      die("error: write");

    delay(1000/120);
    
    if(toggle_timeout < 119)
      toggle_timeout++;
  }

  // Destroy udev
  if (ioctl(fd, UI_DEV_DESTROY) < 0)
    die("error: ioctl");

  close(fd);
  
  return 0;
}
