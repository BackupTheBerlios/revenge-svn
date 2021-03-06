/*
 *  Copyright (C) 2004-2009 The libbeauty Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 * 11-9-2004 Initial work.
 *   Copyright (C) 2004 James Courtier-Dutton James@superbug.co.uk
 */

#ifndef __BFL__
#define __BFL__

#include <bfd.h>
#include <inttypes.h>
#include <rev.h>

const char *bfd_err(void);

struct rev_eng *bf_test_open_file(const char *fn);
int bf_get_arch_mach(struct rev_eng *handle, uint32_t *arch, uint64_t *mach);
void bf_test_close_file(struct rev_eng *r);
int64_t bf_get_code_size(struct rev_eng* ret);
int bf_copy_code_section(struct rev_eng* ret, uint8_t *data, uint64_t data_size);
int64_t bf_get_data_size(struct rev_eng* ret);
int bf_copy_data_section(struct rev_eng* ret, uint8_t *data, uint64_t data_size);
int bf_get_reloc_table_code_section(struct rev_eng* ret);
int bf_get_reloc_table_data_section(struct rev_eng* ret);

#endif /* __BFL__ */
