/*
 * active port forwarder - software for secure forwarding
 * Copyright (C) 2003,2004,2005 jeremian <jeremian [at] poczta.fm>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include "header_buffer_struct.h"

/*
 * Function name: HeaderBuffer_new
 * Description: Create and initialize new HeaderBuffer structure.
 * Returns: Pointer to newly created HeaderBuffer structure.
 */

HeaderBuffer*
HeaderBuffer_new()
{
  HeaderBuffer* tmp = calloc(1, sizeof(HeaderBuffer));
  if (tmp == NULL) {
    return NULL;
  }
  return tmp;
}

/*
 * Function name: HeaderBuffer_free
 * Description: Free the memory allocated for HeaderBuffer structure.
 * Arguments: hb - pointer to pointer to HeaderBuffer structure
 */

void
HeaderBuffer_free(HeaderBuffer** hb)
{
  if (hb == NULL) {
    return;
  }
  if ((*hb) == NULL) {
    return;
  }
  free((*hb));
  (*hb) = NULL;
}

/*
 * Function name: HeaderBuffer_to_read
 * Description: Evaluate how much bytes are needed to fill the header buffer.
 * Arguments: hb - pointer to HeaderBuffer structure
 * Returns: How much bytes are needed to fill the header buffer.
 */

int
HeaderBuffer_to_read(HeaderBuffer* hb)
{
  if (hb == NULL) {
    return -1;
  }
  return (5 - hb->readed);
}

/*
 * Function name: HeaderBuffer_store
 * Description: Store readed part of the header in the buffer.
 * Arguments: hb - pointer to HeaderBuffer structure
 *            buff - reader bytes
 *            n - how much bytes were readed
 */

void
HeaderBuffer_store(HeaderBuffer* hb, unsigned char* buff, int n)
{
  if (hb == NULL) {
    return;
  }
  if ((hb->readed + n) > 5) {
    return;
  }
  if (n <= 0) {
    return;
  }
  memcpy(&hb->tab[hb->readed], buff, n);
  hb->readed += n;
}

/*
 * Function name: HeaderBuffer_restore
 * Description: Restore the full header from the buffer. It's not checked, if the full header is in the buffer.
 * Arguments: hb - pointer to HeaderBuffer structure
 *            buff - place to restore the full header to
 */

void
HeaderBuffer_restore(HeaderBuffer* hb, unsigned char* buff)
{
  if (hb == NULL) {
    return;
  }
  memcpy(buff, hb->tab, 5);
  hb->readed = 0;
}