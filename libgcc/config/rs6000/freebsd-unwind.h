/* DWARF2 EH unwinding support for PowerPC64 FreeBSD.
   Copyright (C) 2012-2016 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

#define R_LR		65

#define MD_FROB_UPDATE_CONTEXT frob_update_context

static void
frob_update_context (struct _Unwind_Context *context,
		     _Unwind_FrameState *fs ATTRIBUTE_UNUSED)
{
  const unsigned int *pc = (const unsigned int *) context->ra;

#ifdef __powerpc64__
  if (fs->regs.reg[2].how == REG_UNSAVED)
    {
      /* If the current unwind info (FS) does not contain explicit info
	 saving R2, then we have to do a minor amount of code reading to
	 figure out if it was saved.  The big problem here is that the
	 code that does the save/restore is generated by the linker, so
	 we have no good way to determine at compile time what to do.  */
      if (pc[0] == 0xF8410028
	  || ((pc[0] & 0xFFFF0000) == 0x3D820000
	      && pc[1] == 0xF8410028))
	{
	  /* We are in a plt call stub or r2 adjusting long branch stub,
	     before r2 has been saved.  Keep REG_UNSAVED.  */
	}
      else
	{
	  unsigned int *insn
	    = (unsigned int *) _Unwind_GetGR (context, R_LR);
	  if (insn && *insn == 0xE8410028)
	    _Unwind_SetGRPtr (context, 2, context->cfa + 40);
	  else if (pc[0] == 0x4E800421
		   && pc[1] == 0xE8410028)
	    {
	      /* We are at the bctrl instruction in a call via function
		 pointer.  gcc always emits the load of the new R2 just
		 before the bctrl so this is the first and only place
		 we need to use the stored R2.  */
	      _Unwind_Word sp = _Unwind_GetGR (context, 1);
	      _Unwind_SetGRPtr (context, 2, (void *)(sp + 40));
	    }
	}
    }
#endif
}
