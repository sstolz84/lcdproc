/** \file server/drivers/ax93304.c
 * LCDd \c ax93304 driver for AX93304 LCD modules by Axiomtek
 */

/* Code file for AX93304 driver
 * for LCDproc LCD software
 * by Nathan Yawn, yawn@emacinc.com
 * 3/24/01
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#ifdef SOLARIS
#  include <strings.h>
#endif
#include "lcd.h"
#include "ax93304.h"
#include "shared/report.h"
#include "lcd_lib.h"
#include "adv_bignum.h"

#define NUM_CCs 8 /* number of characters */

#define AX93304_DEFAULT_DEVICE	"/dev/lcd"


/** private data for the \c ax93304 driver */
typedef struct ax93304_private_data {
  char device[256];
  int speed;
  int fd;
  int width;
  int height;
  int cellwidth;
  int cellheight;
  char *framebuf;
  CGmode ccmode;
} PrivateData;


// Vars for the server core
MODULE_EXPORT char * api_version = API_VERSION;
MODULE_EXPORT int stay_in_foreground = 0;
MODULE_EXPORT int supports_multiple = 0;
MODULE_EXPORT char *symbol_prefix = "ax93304_";


/**
 * Initialize the driver.
 * \param drvthis  Pointer to driver structure.
 * \retval 0       Success.
 * \retval <0      Error.
 */
MODULE_EXPORT int
ax93304_init(Driver *drvthis)
{
  PrivateData *p;
  struct termios portset;

  /* Allocate and store private data */
  p = (PrivateData *) calloc(1, sizeof(PrivateData));
  if (p == NULL)
    return -1;
  if (drvthis->store_private_ptr(drvthis, p))
    return -1;

  /* initialize private data */
  p->fd = -1;
  p->speed = B9600;
  p->width = 16;
  p->height = 2;
  p->cellwidth = 5;
  p->cellheight = 8;
  p->framebuf = NULL;
  p->ccmode = standard;


  /* Read config file */

  /* What device should be used */
  strncpy(p->device, drvthis->config_get_string(drvthis->name, "Device", 0,
						   AX93304_DEFAULT_DEVICE), sizeof(p->device));
  p->device[sizeof(p->device)-1] = '\0';
  report(RPT_INFO, "%s: using Device %s", drvthis->name, p->device);

  /* What speed to use */
  p->speed = drvthis->config_get_int(drvthis->name, "Speed", 0, 9600);

  if (p->speed == 1200)       p->speed = B1200;
  else if (p->speed == 2400)  p->speed = B2400;
  else if (p->speed == 9600)  p->speed = B9600;
  else if (p->speed == 19200) p->speed = B19200;
  else {
    report(RPT_WARNING, "%s: illegal Speed %d; must be one of 1200, 2400, 9600 or 19200; using default %d",
		    drvthis->name, p->speed, 9600);
    p->speed = B9600;
  }

  // Set up io port correctly, and open it...
  p->fd = open(p->device, O_RDWR | O_NOCTTY | O_NDELAY);
  if (p->fd == -1) {
    report(RPT_ERR, "%s: open(%s) failed (%s)", drvthis->name, p->device, strerror(errno));
    return -1;
  }

  //debug(RPT_DEBUG, "ax93304_init: opened device %s", device);

  tcflush(p->fd, TCIOFLUSH);

  // We use RAW mode
#ifdef HAVE_CFMAKERAW
  // The easy way
  cfmakeraw(&portset);
#else
  // The hard way
  portset.c_iflag &= ~( IGNBRK | BRKINT | PARMRK | ISTRIP
                        | INLCR | IGNCR | ICRNL | IXON );
  portset.c_oflag &= ~OPOST;
  portset.c_lflag &= ~( ECHO | ECHONL | ICANON | ISIG | IEXTEN );
  portset.c_cflag &= ~( CSIZE | PARENB | CRTSCTS );
  portset.c_cflag |= CS8 | CREAD | CLOCAL ;
#endif

  portset.c_cc[VTIME] = 0;  // Don't use the timer, no workee
  portset.c_cc[VMIN] = 1;  // Need at least 1 char

  // Set port speed
  cfsetospeed(&portset, B9600);
  cfsetispeed(&portset, B0);

  // Do it...
  tcsetattr(p->fd, TCSANOW, &portset);
  tcflush(p->fd, TCIOFLUSH);

  /*------------------------------------*/

  p->framebuf = malloc(p->width * p->height);
  if (p->framebuf == NULL) {
    ax93304_close(drvthis);
    report(RPT_ERR, "%s: Error: unable to create framebuffer", drvthis->name);
    return -1;
  }
  memset(p->framebuf, ' ', p->width * p->height);

  /*** Open the port write-only, then fork off a process that reads chars ?!!? ***/

  /* Reset and clear the AX93304 */
  write(p->fd, "\xfe\x01\xfe\x02", 4);  // clear screen, home SStolz

  report(RPT_DEBUG, "%s: init() done", drvthis->name);

  return 0;
}


/**
 * Close the driver (do necessary clean-up).
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
ax93304_close(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  //debug(RPT_DEBUG, "Closing AX93304");
  if (p != NULL) {
    if (p->fd >= 0) {
      write(p->fd, "\xfe\x08\xfb\xfc", 4);  // Backlight and Display OFF SStolz
      close(p->fd);
    }

    if (p->framebuf != NULL)
      free(p->framebuf);

    free(p);
  }
  drvthis->store_private_ptr(drvthis, NULL);
}


/**
 * Return the display width in characters.
 * \param drvthis  Pointer to driver structure.
 * \return         Number of characters the display is wide.
 */
MODULE_EXPORT int
ax93304_width(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  return p->width;
}


/**
 * Return the display height in characters.
 * \param drvthis  Pointer to driver structure.
 * \return         Number of characters the display is high.
 */
MODULE_EXPORT int
ax93304_height(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  return p->cellheight;
}


/**
 * Return the width of a character in pixels.
 * \param drvthis  Pointer to driver structure.
 * \return         Number of pixel columns a character cell is wide.
 */
MODULE_EXPORT int
ax93304_cellwidth(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  return p->width;
}


/**
 * Return the height of a character in pixels.
 * \param drvthis  Pointer to driver structure.
 * \return         Number of pixel lines a character cell is high.
 */
MODULE_EXPORT int
ax93304_cellheight(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  return p->cellheight;
}


/**
 * Clear the screen.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
ax93304_clear(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  memset(p->framebuf, ' ', p->width * p->height);
  p->ccmode = standard;
}


/**
 * Flush data on screen to the LCD.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
ax93304_flush(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  //debug(RPT_DEBUG, "AX93304 flush");
  write(p->fd, "\xfe\x01\xfe\x02", 4);  // Clear, Goto Line 1 Char 1
  write(p->fd, p->framebuf, 16);
  write(p->fd, "\xfe\xc0", 2);  // Goto Line 2, Char 1
  write(p->fd, p->framebuf+16, 16);
}


/**
 * Print a string on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width, p->height).
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param string   String that gets written.
 */
MODULE_EXPORT void
ax93304_string(Driver *drvthis, int x, int y, const char string[])
{
  PrivateData *p = drvthis->private_data;
  int i;

  //debug(RPT_DEBUG, "Putting string %s at %i, %i", string, x, y);

  x--;  // Convert 1-based coords to 0-based...
  y--;

  for (i = 0; string[i] != '\0'; i++) {
    unsigned char c = (unsigned char) string[i];

    // Check for buffer overflows...
    if ((y * p->width) + x + i > (p->width * p->height))
      break;

    if ((c > 0x7F) && (c < 0x98)) {
      report(RPT_WARNING, "%s: illegal char 0x%02X requested in ax93304_string()",
			 drvthis->name, c);
      c = ' ';
    }

    p->framebuf[(y * p->width) + x + i] = c;
  }
}


/**
 * Print a character on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width, p->height).
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param c        Character that gets written.
 */
MODULE_EXPORT void
ax93304_chr(Driver *drvthis, int x, int y, char c)
{
  PrivateData *p = drvthis->private_data;
  unsigned char ch = (unsigned char) c;

  //debug(RPT_DEBUG, "Putting char %c (%#x) at %i, %i", c, c, x, y);

  y--;
  x--;

  if ((ch > 0x7F) && (ch < 0x98)) {
    report(RPT_WARNING, "%s: illegal char 0x%02X requested in ax93304_chr()",
			drvthis->name, c);
    ch = ' ';
  }

  /* No shifting the custom chars here, so ax93304_chr() can beep */
  p->framebuf[(y * p->width) + x] = ch;
}


/**
 * Turn the LCD backlight on or off.
 * \param drvthis  Pointer to driver structure.
 * \param on       New backlight status.
 */
MODULE_EXPORT void
ax93304_backlight(Driver *drvthis, int on)
{
  PrivateData *p = drvthis->private_data;

  debug(RPT_DEBUG, "Backlight %s", (on) ? "ON" : "OFF");

  if (on) {
    write(p->fd, "\xfb\xfb", 2); // SStolz
  }
  else {
    write(p->fd, "\xfb\xfc", 2); // SStolz
  }
}


/**
 * Define a custom character and write it to the LCD.
 * \param drvthis  Pointer to driver structure.
 * \param n        Custom character to define [0 - (NUM_CCs-1)].
 * \param dat      Array of 8 (= cellheight) bytes, each representing a row in
 *                 CGRAM starting from the top.
 */
MODULE_EXPORT void
ax93304_set_char(Driver *drvthis, int n, unsigned char *dat)
{
  PrivateData *p = drvthis->private_data;
  int row;
  unsigned char mask = (1 << p->cellwidth) - 1;
 
  //debug(RPT_DEBUG, "Set char %i", n);
 
  if ((n < 0) || (n >= NUM_CCs)) /* Do we want to the aliased indexes as well (0x98 - 0x9F?) */
    return;
 
  if (!dat)
    return;
 
  /* Set the LCD to accept data for rewrite-able char n */
  write(p->fd, "\xfe", 1);
  write(p->fd, 0x40 + (n * 8), 1);
 
  for (row = 0; row < p->cellheight; row++) {
    char letter = dat[row] & mask;
    write(p->fd, &letter, 1);
  }
 
  /* return the LCD to normal operation */
  write(p->fd, "\xfe\xff", 2);
}


/**
 * Draw a vertical bar bottom-up.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column) of the starting point.
 * \param y        Vertical character position (row) of the starting point.
 * \param len      Number of characters that the bar is high at 100%
 * \param promille Current height level of the bar in promille.
 * \param options  Options (currently unused).
 */
MODULE_EXPORT void
ax93304_vbar(Driver *drvthis, int x, int y, int len, int promille, int options)
{
  PrivateData *p = drvthis->private_data;
  static unsigned char bar_up[7][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F},
    {0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x1F},
    {0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F},
    {0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F},
    {0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F},
  };

  //debug(RPT_DEBUG, "Vbar at %i, length %i", x, len);

  if (p->ccmode != vbar) {
    int i;

    if (p->ccmode != standard) {
      /* Not supported (yet) */
      report(RPT_WARNING, "%s: cannot combine two modes using user-defined characters",
		      drvthis->name);
      return;
    }
    p->ccmode = vbar;

    for (i = 0; i < 7; i++)
      ax93304_set_char(drvthis, i + 1, bar_up[i]);
  }

  lib_vbar_static(drvthis, x, y, len, promille, options, p->cellheight, 0x01);
}


/**
 * Draw a horizontal bar to the right.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column) of the starting point.
 * \param y        Vertical character position (row) of the starting point.
 * \param len      Number of characters that the bar is long at 100%
 * \param promille Current length level of the bar in promille.
 * \param options  Options (currently unused).
 */
MODULE_EXPORT void
ax93304_hbar(Driver *drvthis, int x, int y, int len, int promille, int options)
{
  PrivateData *p = drvthis->private_data;
  static unsigned char bar_right[4][8] = {
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
    {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C},
    {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E},
  };

  //debug(RPT_DEBUG, "Hbar at %i,%i; length %i", x, y, len);

  if (p->ccmode != hbar) {
    int i;

    if (p->ccmode != standard) {
      report(RPT_WARNING, "%s: cannot combine two modes using user-defined characters",
	     drvthis->name);
      return;
    }
    p->ccmode = hbar;

    for (i = 0; i < 4; i++)
      ax93304_set_char(drvthis, i + 1, bar_right[i]);
  }

  lib_hbar_static(drvthis, x, y, len, promille, options, p->cellwidth, 0x01);
}


/**
 * Place an icon on the screen.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param icon     synbolic value representing the icon.
 * \retval 0       Icon has been successfully defined/written.
 * \retval <0      Server core shall define/write the icon.
 */
MODULE_EXPORT int
ax93304_icon(Driver *drvthis, int x, int y, int icon)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;
	
	static unsigned char heart_open[] =
		{ b__XXXXX,
		  b__X_X_X,
		  b_______,
		  b_______,
		  b_______,
		  b__X___X,
		  b__XX_XX,
		  b__XXXXX };
	static unsigned char heart_filled[] =
		{ b__XXXXX,
		  b__X_X_X,
		  b___X_X_,
		  b___XXX_,
		  b___XXX_,
		  b__X_X_X,
		  b__XX_XX,
		  b__XXXXX };
	static unsigned char arrow_up[] =
		{ b____X__,
		  b___XXX_,
		  b__X_X_X,
		  b____X__,
		  b____X__,
		  b____X__,
		  b____X__,
		  b_______ };
	static unsigned char arrow_down[] =
		{ b____X__,
		  b____X__,
		  b____X__,
		  b____X__,
		  b__X_X_X,
		  b___XXX_,
		  b____X__,
		  b_______ };
	static unsigned char checkbox_off[] =
		{ b_______,
		  b_______,
		  b__XXXXX,
		  b__X___X,
		  b__X___X,
		  b__X___X,
		  b__XXXXX,
		  b_______ };
	static unsigned char checkbox_on[] =
		{ b____X__,
		  b____X__,
		  b__XXX_X,
		  b__X_XX_,
		  b__X_X_X,
		  b__X___X,
		  b__XXXXX,
		  b_______ };
	static unsigned char checkbox_gray[] =
		{ b_______,
		  b_______,
		  b__XXXXX,
		  b__X_X_X,
		  b__XX_XX,
		  b__X_X_X,
		  b__XXXXX,
		  b_______ };
	static unsigned char block_filled[] =
		{ b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX };

	/* Icons from CGROM will always work */
	switch (icon) {
	    case ICON_ARROW_LEFT:
		ax93304_chr(drvthis, x, y, 0x1B);
		return 0;
	    case ICON_ARROW_RIGHT:
		ax93304_chr(drvthis, x, y, 0x1A);
		return 0;
	}

	/* The heartbeat icons do not work in bignum and vbar mode */
	if ((icon == ICON_HEART_FILLED) || (icon == ICON_HEART_OPEN)) {
		if ((p->ccmode != bignum) && (p->ccmode != vbar)) {
			switch (icon) {
			    case ICON_HEART_FILLED:
				ax93304_set_char(drvthis, 7, heart_filled);
				ax93304_chr(drvthis, x, y, 7);
				return 0;
			    case ICON_HEART_OPEN:
				ax93304_set_char(drvthis, 7, heart_open);
				ax93304_chr(drvthis, x, y, 7);
				return 0;
			}
		}
		else {
			return -1;
		}
	}

	switch (icon) {
		case ICON_ARROW_UP:
			ax93304_set_char(drvthis, 1, arrow_up);
			ax93304_chr(drvthis, x, y, 1);
			break;
		case ICON_ARROW_DOWN:
			ax93304_set_char(drvthis, 2, arrow_down);
			ax93304_chr(drvthis, x, y, 2);
			break;
		case ICON_CHECKBOX_OFF:
			ax93304_set_char(drvthis, 3, checkbox_off);
			ax93304_chr(drvthis, x, y, 3);
			break;
		case ICON_CHECKBOX_ON:
			ax93304_set_char(drvthis, 4, checkbox_on);
			ax93304_chr(drvthis, x, y, 4);
			break;
		case ICON_CHECKBOX_GRAY:
			ax93304_set_char(drvthis, 5, checkbox_gray);
			ax93304_chr(drvthis, x, y, 5);
			break;
		default:
			return -1;	/* Let the core do other icons */
	}
	return 0;
}


/**
 * Get key from the device.
 * \param drvthis  Pointer to driver structure.
 * \return         String representation of the key;
 *                 \c NULL if nothing available / unmapped key.
 */
MODULE_EXPORT const char *
ax93304_get_key(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;
  fd_set brfdset;
  struct timeval twait;
  char *key = NULL;
 
  //debug(RPT_DEBUG, "AX93304 get_key...");
 
  /* Check for incoming data.  Turn backlight ON/OFF as needed */
 
  FD_ZERO(&brfdset);
  FD_SET(p->fd, &brfdset);
 
  twait.tv_sec = 0;
  twait.tv_usec = 0;
 
  /* read key from LCD */
  write(p->fd, "\xfd", 1);
 
  if (select(p->fd + 1, &brfdset, NULL, NULL, &twait)) {
    char ch;
    int retval = read(p->fd, &ch, 1);
 
    if (retval > 0) {	/* read() succeeded and returned data */
      debug(RPT_INFO, "ax93304_get_key: Got key: %c", ch);
 
      switch (ch) {
	case 'M': key = "Up";		/* YES/+ key pressed */
		  break;
	case 'G': key = "Down";		/* NO/- key pressed */
		  break;
	case 'K': key = "Escape";	/* MENU key pressed */
		  break;
	case 'N': key = "Enter";	/* SELECT key pressed */
		  break;
	default:  break;
      }
    }
    else {	/* read() error */
      report(RPT_ERR, "%s: Read error in AX93304 getchar", drvthis->name);
    }
  }
  else {
      ;//debug(RPT_DEBUG, "No AX93304 data present");
  }
 
  return key;
}
