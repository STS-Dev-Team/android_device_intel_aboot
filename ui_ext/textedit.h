/*
   (c) Copyright 2001-2002  convergence integrated media GmbH.
   (c) Copyright 2002-2005  convergence GmbH.
   (c) Copyright 2011 Borqs Ltd.

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#ifndef __TEXTEDIT_H__
#define __TEXTEDIT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <lite/box.h>
#include <lite/theme.h>

/* @brief Macro to convert a generic LiteBox into a LiteTextEdit */
#define LITE_TEXTEDIT(l) ((LiteTextEdit*)(l))

/* @brief LiteTextEdit structure */
typedef struct _LiteTextEdit LiteTextEdit;

/* @brief Textedit theme */
typedef struct _LiteTextEditTheme {
     LiteTheme theme;           /**< base LiTE theme */

     LiteFont *font;            /**< text edit font */
     DFBColor *font_color;      /**< text edit font color */
     int font_size;             /**< text edit font size  */
} LiteTextEditTheme;

/* @brief Standard text edit themes */
/* @brief No text edit theme */
#define liteNoTextEditTheme     NULL

/* @brief default text edit theme */
extern LiteTextEditTheme *liteDefaultTextEditTheme;


/* @brief TextEdit enter callback prototype 
 * This callback is use for installing a callback to trigger
 * when the enter key is triggered in a focused LiteTextEdit.
 *
 * @param text                  IN:     Text in the text edit
 * @param data                  IN:     Data context
 * 
 * @return void
 */     
typedef void (*TextEditEnterFunc) (const char *text, void *data);

/* @brief TextEdit abort callback prototype 
 * This callback is use for installing a callback to trigger
 * when the Escape key is triggered in a focused LiteTextEdit.
 *
 * @param data                  IN:     Data context
 * 
 * @return void
 */     
typedef void (*TextEditAbortFunc) (void *data);

/* @brief Create a new LiteTextEdit object
 * This function will create a new LiteTextEdit object.
 *
 * @param parent                IN:     Valid parent LiteBox
 * @param rect                  IN:     Rectangle for the text edit
 * @param theme                 IN:     Text edit theme
 * @param font_size             IN:     Font size
 * @param ret_textedit          OUT:    Valid LiteTextEdit object
 * 
 * @return DFBResult            Returns DFB_OK if successful.     
 */
DFBResult lite_new_textedit(LiteBox           *parent,
                            DFBRectangle      *rect,
                            LiteTextEditTheme *theme,
                            int                font_size,
                            LiteTextEdit     **ret_textedit);

/* @brief Set the text field of a text edit.
 * This function will set the text field of a text edit.
 * 
 * @param textedit              IN:     Valid LiteTextEdit object
 * @param text                  IN:     text string
 *
 * @return DFBResult            Returns DFB_OK if successful.     
 */
DFBResult lite_set_textedit_text (LiteTextEdit *textedit, const char *text);
DFBResult lite_get_textedit_text (LiteTextEdit *textedit, char **ret_text);

/* @brief Enable/Disable cursor of a text edit.
 * This function will enable/disable cursor a text edit.
 * 
 * @param textedit              IN:     Valid LiteTextEdit object
 * @param enable                IN:     enable
 *
 * @return DFBResult            Returns DFB_OK if successful.     
 */
DFBResult lite_enable_textedit_cursor(LiteTextEdit *textedit, const bool enable);

/* @brief On/Off the focus of a text edit.
 * This function will enable/disable focus a text edit.
 * 
 * @param textedit              IN:     Valid LiteTextEdit object
 * @param on                    IN:     on
 *
 * @return DFBResult            Returns DFB_OK if successful.     
 */
DFBResult lite_set_textedit_focus(LiteTextEdit *textedit, const bool on);

/* @brief Install the textedit enter callback function.
 * This function will set the textedit enter callback function
 * that is triggered when the ENTER key is called.
 * 
 * @param textedit              OUT:    Valid LiteTextEdit object
 * @param callback              IN:     Valid callback
 * @param data                  IN:     Data context.
 *
 * @return DFBResult            Returns DFB_OK if successful.     
 */
DFBResult lite_on_textedit_enter(LiteTextEdit     *textedit,
                                 TextEditEnterFunc callback,
                                 void             *data);

/* @brief Install the textedit abort callback function.
 * This function will set the textedit abort callback function
 * that is triggered when the ESCAPE key is called.
 * 
 * @param textedit              OUT:    Valid LiteTextEdit object
 * @param callback              IN:     Valid callback
 * @param data                  IN:     Data context.
 *
 * @return DFBResult            Returns DFB_OK if successful.     
 */
DFBResult lite_on_textedit_abort(LiteTextEdit     *textedit,
                                 TextEditAbortFunc callback,
                                 void             *data);

#ifdef __cplusplus
}
#endif

#define MAX_ROWS        (29)
#define MAX_COLS        (55)

#endif /*  __TEXTEDIT_H__  */
