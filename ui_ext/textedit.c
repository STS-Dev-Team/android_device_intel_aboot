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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <direct/mem.h>

#include <lite/util.h>
#include <lite/font.h>
#include <lite/window.h>

#include "textedit.h"
#include "../recovery.h"

LiteTextEditTheme *liteDefaultTextEditTheme = NULL;

struct _LiteTextEdit {
     LiteBox            box;
     LiteTextEditTheme *theme;

     LiteFont          *font;
     char              *text;
     unsigned int       cursor_pos;
     bool               cursor_enable;
     int                modified;

     char              *backup;

     TextEditEnterFunc  enter;
     void              *enter_data;

     TextEditAbortFunc  abort;
     void              *abort_data;
};

static int on_focus_in(LiteBox *box);
static int on_focus_out(LiteBox *box);
static int on_key_down(LiteBox *box, DFBWindowEvent *ev);
static int on_button_down(LiteBox *box, int x, int y, DFBInputDeviceButtonIdentifier button);

static DFBResult draw_textedit(LiteBox *box, const DFBRegion *region, DFBBoolean clear);
static DFBResult destroy_textedit(LiteBox *box);

static int text_cols = 0, text_rows = 0;
static int text_col = 0, text_row = 0, text_top = 0;

char text[MAX_ROWS + 1][MAX_COLS] = {{'\0', }, };

DFBResult 
lite_new_textedit(LiteBox           *parent, 
                  DFBRectangle      *rect,
                  LiteTextEditTheme *theme,
                  int                font_size,
                  LiteTextEdit     **ret_textedit)
{
     DFBResult      res;
     LiteTextEdit  *textedit = NULL;
     IDirectFBFont *font_interface = NULL;

     textedit = D_CALLOC(1, sizeof(LiteTextEdit));

     textedit->box.parent = parent;
     textedit->theme = theme;
     textedit->box.rect = *rect;

     res = lite_init_box(LITE_BOX(textedit));
     if (res != DFB_OK) {
          D_FREE(textedit);
          return res;
     }

     if (font_size <= 0 || font_size >= (rect->h *9/10 - 6))
          font_size = rect->h *9/10 - 6;
     res = lite_get_font("default", LITE_FONT_PLAIN, font_size,
                              DEFAULT_FONT_ATTRIBUTE, &textedit->font);
     if (res != DFB_OK) {
          D_FREE(textedit);
          return res;
     }

     res = lite_font(textedit->font, &font_interface);
     if (res != DFB_OK) {
          D_FREE(textedit);
          return res;
     }

     textedit->box.type    = LITE_TYPE_TEXTLINE;
     textedit->box.Draw    = draw_textedit;
     textedit->box.Destroy = destroy_textedit;

     textedit->box.OnFocusIn    = on_focus_in;
     textedit->box.OnFocusOut   = on_focus_out;
     textedit->box.OnKeyDown    = on_key_down;
     textedit->box.OnButtonDown = on_button_down;

     textedit->text = D_STRDUP("");
     textedit->cursor_enable = false;

     textedit->box.surface->SetFont(textedit->box.surface, font_interface);

     res = lite_update_box(LITE_BOX(textedit), NULL);
     if (res != DFB_OK) {
          D_FREE(textedit);
          return res;
     }
     
     *ret_textedit = textedit;

     return DFB_OK;
}

DFBResult
lite_enable_textedit_cursor(LiteTextEdit *textedit, const bool enable)
{
     LITE_NULL_PARAMETER_CHECK(textedit);
     LITE_BOX_TYPE_PARAMETER_CHECK(LITE_BOX(textedit), LITE_TYPE_TEXTLINE);

     textedit->cursor_enable = enable;

     return DFB_OK;
}

DFBResult
lite_set_textedit_focus(LiteTextEdit *textedit, const bool on)
{
     LITE_NULL_PARAMETER_CHECK(textedit);
     LITE_BOX_TYPE_PARAMETER_CHECK(LITE_BOX(textedit), LITE_TYPE_TEXTLINE);

     if (LITE_BOX(textedit)->is_focused != on)
          LITE_BOX(textedit)->is_focused = on;

     return DFB_OK;
}

#define TOTAL_BUF_SIZE      (5*1024*1024)
char str[TOTAL_BUF_SIZE] = {'\0', };
DFBResult 
lite_set_textedit_text(LiteTextEdit *textedit, const char *text)
{
     LITE_NULL_PARAMETER_CHECK(textedit);
     LITE_BOX_TYPE_PARAMETER_CHECK(LITE_BOX(textedit), LITE_TYPE_TEXTLINE);
     LITE_NULL_PARAMETER_CHECK(text);

     strcat(str, text);
     text = str;

     if (!strcmp(textedit->text, text)) {
          if (!textedit->modified)
               return DFB_OK;
     }
     else {
          if (textedit->modified)
               D_FREE(textedit->backup);

          D_FREE(textedit->text);

          textedit->text = D_STRDUP(text);
          textedit->cursor_pos = strlen(text);
     }

     textedit->modified = 0;

     return lite_update_box(LITE_BOX(textedit), NULL);
}

DFBResult
lite_get_textedit_text (LiteTextEdit *textedit, char **ret_text)
{
     LITE_NULL_PARAMETER_CHECK(textedit);
     LITE_BOX_TYPE_PARAMETER_CHECK(LITE_BOX(textedit), LITE_TYPE_TEXTLINE);
     LITE_NULL_PARAMETER_CHECK(ret_text);

     *ret_text = D_STRDUP(textedit->text);

     return DFB_OK;
}

DFBResult 
lite_on_textedit_enter(LiteTextEdit      *textedit,
                         TextEditEnterFunc  func,
                         void              *funcdata)
{
     LITE_NULL_PARAMETER_CHECK(textedit);
     LITE_BOX_TYPE_PARAMETER_CHECK(LITE_BOX(textedit), LITE_TYPE_TEXTLINE);

     textedit->enter      = func;
     textedit->enter_data = funcdata;

     return DFB_OK;
}

DFBResult 
lite_on_textedit_abort(LiteTextEdit      *textedit,
                         TextEditAbortFunc  func,
                         void              *funcdata)
{
     LITE_NULL_PARAMETER_CHECK(textedit);
     LITE_BOX_TYPE_PARAMETER_CHECK(LITE_BOX(textedit), LITE_TYPE_TEXTLINE);

     textedit->abort      = func;
     textedit->abort_data = funcdata;

     return DFB_OK;
}

/* internals */

static DFBResult 
destroy_textedit(LiteBox *box)
{
     LiteTextEdit *textedit = NULL;

     D_ASSERT(box != NULL);

     textedit = LITE_TEXTEDIT(box);

     if (!textedit)
          return DFB_FAILURE;

     if (textedit->modified)
          D_FREE(textedit->backup);

     D_FREE(textedit->text);

     return lite_destroy_box(box);
}

void update_screen_locked(IDirectFBSurface *surface)
{
	int i = 0;
	for (; i < text_rows; ++i) {
		surface->DrawString(surface, text[(i+text_top) % text_rows], -1, 0, i * 20, DSTF_TOPLEFT);
	}

}

static int ui_print(char *buf, IDirectFBSurface *surface)
{
    if (text_rows > 0 && text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
			if (*ptr == '\n' || *ptr == '\b' || *ptr == '\t' || ((*ptr >= 0x20) && (*ptr <= 0x7E))) {
				if (*ptr == '\n' || text_col >= text_cols) {
					text[text_row][text_col] = '\0';
					text_col = 0;
					text_row = (text_row + 1) % text_rows;
					if (text_row == text_top) text_top = (text_top + 1) % text_rows;
				}
				if (*ptr != '\n') {
					if (*ptr == '\b') {
						if (text_col != 0) text_col -= 1;
						continue;
					} else if (*ptr == '\t') {
						text[text_row][text_col++] = ' ';
					} else {
						text[text_row][text_col++] = *ptr;
					}
				}
			}
        }
        text[text_row][text_col] = '\0';
		update_screen_locked(surface);
    }
	return 0;
}

static int ui_init(int font_height, const DFBRegion *region)
{
	text_col = 0;
	text_row = 0;

	text_rows = MAX_ROWS;
	text_cols = MAX_COLS;
	text_top = 1;
	return 0;
}

static int get_buf(char *buf, const char *dest) {

	int GetStringLengh = strlen(dest);
	if (GetStringLengh < MAX_SIZE) {
		strncpy(buf, dest, GetStringLengh);
	} else {
		strncpy(buf, (dest + GetStringLengh - MAX_SIZE), MAX_SIZE);
	}
	return 0;
}

static DFBResult 
draw_textedit(LiteBox         *box, 
              const DFBRegion *region, 
              DFBBoolean       clear)
{
     DFBResult         result;
     IDirectFBSurface *surface  = box->surface;
     LiteTextEdit     *textedit = LITE_TEXTEDIT(box);
     int               surface_x, cursor_x = 0;
     IDirectFBFont    *font_interface = NULL;
     int               font_size;

     D_ASSERT(box != NULL);

     result = lite_font(textedit->font, &font_interface);
     if (result != DFB_OK)
          return result;
     lite_get_font_size(textedit->font, &font_size);

     surface->SetClip(surface, region);

     /* Draw border */
     surface->SetDrawingFlags(surface, DSDRAW_NOFX);

     if (box->is_focused)
          surface->SetColor(surface, 0xa0, 0xa0, 0xff, 0xff);
     else
          surface->SetColor(surface, 0xe0, 0xe0, 0xe0, 0xff);

     surface->DrawRectangle(surface, 0, 0, box->rect.w, box->rect.h);

     surface->SetColor(surface, 0xc0, 0xc0, 0xc0, 0xff);
     surface->DrawRectangle(surface, 1, 1, box->rect.w - 2, box->rect.h - 2);

     /* Fill the background */
     surface->SetColor(surface, 0x00, 0x00, 0x00, 0x00);

     surface->FillRectangle(surface, 2, 2, box->rect.w - 4, box->rect.h - 4);

     /* Draw the text */
     surface->SetColor(surface, 0xff, 0xff, 0xff, 0xff);

/*
     font_interface->GetStringWidth(font_interface, textedit->text, textedit->cursor_pos, &cursor_x);
*/

	 char buf[MAX_SIZE] = {'\0', };
	 int font_height = 0;
	 font_interface->GetHeight(font_interface, &font_height);
	 get_buf(buf, textedit->text);
	 ui_init(font_height, region);
	 ui_print(buf, surface);

	 surface_x = 0 ;
     cursor_x += surface_x - 5;

     /* Draw the cursor */
     if (textedit->cursor_enable) {
          surface->SetDrawingFlags(surface, DSDRAW_BLEND);
          if (box->is_focused)
               surface->SetColor(surface, 0x40, 0x40, 0x80, 0x80);
          else
               surface->SetColor(surface, 0x80, 0x80, 0x80, 0x80);
          surface->FillRectangle(surface, cursor_x + 5, 4, 1, font_size);
     }

     return DFB_OK;
}

static int 
on_focus_in(LiteBox *box)
{
     D_ASSERT(box != NULL);

     lite_update_box(box, NULL);

     return 0;
}

static int 
on_focus_out(LiteBox *box)
{
     D_ASSERT(box != NULL);

     lite_update_box(box, NULL);

     return 0;
}

static void 
set_modified(LiteTextEdit *textedit)
{
     D_ASSERT(textedit != NULL);

     if (textedit->modified)
          return;

     textedit->modified = true;

     textedit->backup = D_STRDUP(textedit->text);
}

static void 
clear_modified(LiteTextEdit *textedit, bool restore)
{
     D_ASSERT(textedit != NULL);

     if (!textedit->modified)
          return;

     textedit->modified = false;

     if (restore) {
          D_FREE(textedit->text);

          textedit->text = textedit->backup;
     }
     else
          D_FREE(textedit->backup);

     textedit->cursor_pos = 0;
}

static int 
on_key_down(LiteBox *box, DFBWindowEvent *ev)
{
     LiteTextEdit *textedit = LITE_TEXTEDIT(box);
     int           redraw   = 0;

     D_ASSERT(box != NULL);

     switch (ev->key_symbol) {
          case DIKS_ENTER:
               if (textedit->modified) {
                    if (textedit->enter)
                         textedit->enter(textedit->text, textedit->enter_data);

                    clear_modified(textedit, false);

                    redraw = 1;
               }
               break;
          case DIKS_ESCAPE:
               if (textedit->abort)
                    textedit->abort(textedit->abort_data);

               if (textedit->modified) {
                    clear_modified(textedit, true);

                    redraw = 1;
               }
               break;
          case DIKS_CURSOR_LEFT:
               if (textedit->cursor_pos > 0) {
                    textedit->cursor_pos--;
                    redraw = 1;
               }
               break;
          case DIKS_CURSOR_RIGHT:
               if (textedit->cursor_pos < strlen (textedit->text)) {
                    textedit->cursor_pos++;
                    redraw = 1;
               }
               break;
          case DIKS_HOME:
               if (textedit->cursor_pos > 0) {
                    textedit->cursor_pos = 0;
                    redraw = 1;
               }
               break;
          case DIKS_END:
               if (textedit->cursor_pos < strlen (textedit->text)) {
                    textedit->cursor_pos = strlen (textedit->text);
                    redraw = 1;
               }
               break;
          case DIKS_DELETE:
               if (textedit->cursor_pos < strlen (textedit->text)) {
                    int len = strlen (textedit->text);

                    set_modified(textedit);

                    memmove(textedit->text + textedit->cursor_pos,
                             textedit->text + textedit->cursor_pos + 1,
                             len - textedit->cursor_pos);

                    textedit->text = D_REALLOC(textedit->text, len);

                    redraw = 1;
               }
               break;
          case DIKS_BACKSPACE:
               if (textedit->cursor_pos > 0) {
                    int len = strlen(textedit->text);

                    set_modified(textedit);

                    memmove(textedit->text + textedit->cursor_pos - 1,
                             textedit->text + textedit->cursor_pos,
                             len - textedit->cursor_pos + 1);

                    textedit->text = D_REALLOC(textedit->text, len);

                    textedit->cursor_pos--;

                    redraw = 1;
               }
               break;
          default:
               if (ev->key_symbol >= 32 && ev->key_symbol <= 127) {
                    int len = strlen(textedit->text);

                    set_modified(textedit);

                    textedit->text = D_REALLOC(textedit->text, len + 2);

                    memmove(textedit->text + textedit->cursor_pos + 1,
                             textedit->text + textedit->cursor_pos,
                             len - textedit->cursor_pos + 1);

                    textedit->text[textedit->cursor_pos] = ev->key_symbol;

                    textedit->cursor_pos++;

                    redraw = 1;
               }
               else
                    return 0;
     }

     if (redraw)
          lite_update_box(box, NULL);

     return 1;
}

static int 
on_button_down(LiteBox                       *box, 
               int                            x, 
               int                            y,
               DFBInputDeviceButtonIdentifier button)
{
     D_ASSERT(box != NULL);

     lite_focus_box(box);

     return 1;
}

