/* Header file for AX93304 driver
 * for LCDproc LCD software
 * by Nathan Yawn, yawn@emacinc.com
 * 3/24/01
 */


#ifndef _AX93304_H
#define _AX93304_H

MODULE_EXPORT int  ax93304_init(Driver *drvthis);
MODULE_EXPORT void ax93304_close(Driver *drvthis);
MODULE_EXPORT int  ax93304_width(Driver *drvthis);
MODULE_EXPORT int  ax93304_height(Driver *drvthis);
MODULE_EXPORT int  ax93304_cellwidth(Driver *drvthis);
MODULE_EXPORT int  ax93304_cellheight(Driver *drvthis);
MODULE_EXPORT void ax93304_clear(Driver *drvthis);
MODULE_EXPORT void ax93304_flush(Driver *drvthis);
MODULE_EXPORT void ax93304_string(Driver *drvthis, int x, int y, const char string[]);
MODULE_EXPORT void ax93304_chr(Driver *drvthis, int x, int y, char c);

MODULE_EXPORT void ax93304_vbar(Driver *drvthis, int x, int y, int len, int promille, int options);
MODULE_EXPORT void ax93304_hbar(Driver *drvthis, int x, int y, int len, int promille, int options);
MODULE_EXPORT int  ax93304_icon(Driver *drvthis, int x, int y, int icon);

MODULE_EXPORT void ax93304_set_char(Driver *drvthis, int n, unsigned char *dat);

MODULE_EXPORT void ax93304_backlight(Driver *drvthis, int promille);

MODULE_EXPORT const char *ax93304_get_key(Driver *drvthis);

#endif
