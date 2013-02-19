/*
 * weather.c:
 *	Demonstration program to talk to a 4D Systems panel
 *	for a weather application
 *
 *	Gordon Henderson, January 2013
 ***********************************************************************
 */

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <math.h>

#include <geniePi.h>

#ifndef	TRUE
#  define TRUE  (1==1)
#  define FALSE (1==2)
#endif

// GAUGE Offsets

#define	TEMP_BASE	 0
#define	TEMP_BASE_MIN	 7
#define	TEMP_BASE_MAX	14
#define	PRESSURE_BASE	21

// Globals

int temps [8], minTemps [8], maxTemps [8] ;
int currentTemp, minTemp, maxTemp ;


/*
 * updatePressure:
 *	This sends the relevant data to the pressure page
 *	on the displays.
 *
 *	This page has 8 days of history and a live pressure gauge.
 *********************************************************************************
 */

static void updatePressure (int history [8], int live)
{
  int i ;
  int v ;

  for (i = 0 ; i < 8 ; ++i)
  {
    v = history [i] - 940 ;
    if (v < 0)
      v = 0 ;

    genieWriteObj (GENIE_OBJ_GAUGE, PRESSURE_BASE + i, v * 100 / 120) ;
  }

  v = live - 940 ;
  if (v <   0) v =   0 ;
  if (v > 120) v = 120 ;
  genieWriteObj (GENIE_OBJ_COOL_GAUGE, 0, v) ;
}


/*
 * updateTemp:
 *	This sends the relevant data to the current temperature
 *	guagues on the display.
 *
 *	This page has 7 days of history and a live temperature gauge.
 *********************************************************************************
 */

static void updateTemp (int history [7], int value, int base, int thermometer)
{
  int i ;
  int v ;

  for (i = 0 ; i < 7 ; ++i)
  {
    v = history [i] + 10 ;
    if (v > 50) v = 50 ;
    if (v <  0) v =  0 ;

    genieWriteObj (GENIE_OBJ_GAUGE, base + i, v * 2) ;
  }

  v = value  + 10 ;
  if (v > 50) v = 50 ;
  if (v <  0) v =  0 ;
  genieWriteObj (GENIE_OBJ_THERMOMETER, thermometer, v) ;
}


void handleGenieEvent (struct genieReplyStruct *reply)
{
  if (reply->cmd != GENIE_REPORT_EVENT)
  {
    printf ("Invalid event from the display: 0x%02X\r\n", reply->cmd) ;
    return ;
  }

  /**/ if (reply->object == GENIE_OBJ_WINBUTTON)
  {
    /**/ if (reply->index == 2)	// Button 2 -> Reset Min
    {
      minTemp = currentTemp ;
      updateTemp (minTemps, minTemp, TEMP_BASE_MIN, 1) ;
    }
    else if (reply->index == 6)	// Button 6 -> Reset Max
    {
      maxTemp = currentTemp ;
      updateTemp (maxTemps, maxTemp, TEMP_BASE_MAX, 2) ;
    }
    else
      printf ("Unknown button: %d\n", reply->index) ;
  }
  else
    printf ("Unhandled Event: object: %2d, index: %d data: %d [%02X %02X %04X]\r\n",
      reply->object, reply->index, reply->data, reply->object, reply->index, reply->data) ;
}


/*
 * handleTemperature:
 *	This is a thread that runs in a loop, polling the temperature
 *	sensor and updating the display as required.
 *********************************************************************************
 */

static void *handleTemperature (void *data)
{
  double angle ;
  int i, sum ;

  for (i = 0 ; i < 7 ; ++i)
    temps [i] = minTemps [i] = maxTemps [i] = 0 ;

  angle = 0.0 ;

  minTemp =  40 ;
  maxTemp = -10 ;

  for (;;)
  {
    sum = 0 ;
    for (i = 0 ; i < 24 ; ++i)
    {
      currentTemp = (int)rint ((sin (angle / 180.0 * M_PI) + 1.0) * 25.0 - 10.0) ;
      sum += currentTemp ;
      if (currentTemp > maxTemp) maxTemp = currentTemp ;
      if (currentTemp < minTemp) minTemp = currentTemp ;
      updateTemp (temps, currentTemp, TEMP_BASE, 0) ;

      angle += 1.0 ;
      if (angle > 360.0)
 	angle = 0.0 ;

      usleep (100000) ;
    }

    for (i = 1 ; i < 7 ; ++i)
    {
      temps    [i - 1] = temps    [i] ;
      minTemps [i - 1] = minTemps [i] ;
      maxTemps [i - 1] = maxTemps [i] ;
    }

    temps [6] = sum / 24 ;

    minTemps [6] = minTemp ;
    maxTemps [6] = maxTemp ;
    updateTemp (minTemps, minTemp, TEMP_BASE_MIN, 1) ;
    updateTemp (maxTemps, maxTemp, TEMP_BASE_MAX, 2) ;
  }

  return NULL ;
}


/*
 * handlePressure:
 *	This is a thread that runs in a loop, polling the pressure
 *	sensor and updating the display as required.
 *********************************************************************************
 */

static void *handlePressure (void *data)
{
  double angle ;
  int i, sum ;
  int pressure ;
  int pressures [8] ;

  for (i = 0 ; i < 8 ; ++i)
    pressures [i] = 0 ;

  angle = 0.0 ;

  for (;;)
  {
    sum = 0 ;
    for (i = 0 ; i < 24 ; ++i)
    {
      pressure = (int)rint ((sin (angle / 180.0 * M_PI) + 1.0) * 60.0 + 940) ;
      sum += pressure ;
      updatePressure (pressures, pressure) ;

      angle += 1.0 ;
      if (angle > 360.0)
 	angle = 0.0 ;

      usleep (100000) ;
    }

    for (i = 1 ; i < 8 ; ++i)
      pressures [i - 1] = pressures [i] ;

    pressures [7] = sum / 24 ;
  }

  return NULL ;
}


/*
 *********************************************************************************
 * main:
 *	Run our little demo
 *********************************************************************************
 */

int main ()
{
  pthread_t myThread ;
  struct genieReplyStruct reply ;

  printf ("\n\n\n\n") ;
  printf ("Visi-Genie Weather Station Demo\n") ;
  printf ("===============================\n") ;

// Genie display setup
//	Using the Raspberry Pi's on-board serial port.

  if (genieSetup ("/dev/ttyAMA0", 115200) < 0)
  {
    fprintf (stderr, "rgb: Can't initialise Genie Display: %s\n", strerror (errno)) ;
    return 1 ;
  }

// Select form 0 (the temperature)

  genieWriteObj (GENIE_OBJ_FORM, 0, 0) ;

// Start the temperature and pressure sensor reading threads

  (void)pthread_create (&myThread, NULL, handleTemperature, NULL) ;
  (void)pthread_create (&myThread, NULL, handlePressure, NULL) ;

// Big loop - just wait for events from the display now

  for (;;)
  {
    while (genieReplyAvail ())
    {
      genieGetReply    (&reply) ;
      handleGenieEvent (&reply) ;
    }
    usleep (10000) ; // 10mS - Don't hog the CPU in-case anything else is happening...
  }

  return 0 ;
}
