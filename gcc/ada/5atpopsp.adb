------------------------------------------------------------------------------
--                                                                          --
--                GNU ADA RUN-TIME LIBRARY (GNARL) COMPONENTS               --
--                                                                          --
--                SYSTEM.TASK_PRIMITIVES.OPERATIONS.SPECIFIC                --
--                                                                          --
--                                  B o d y                                 --
--                                                                          --
--         Copyright (C) 1992-2004, Free Software Foundation, Inc.          --
--                                                                          --
-- GNARL is free software; you can  redistribute it  and/or modify it under --
-- terms of the  GNU General Public License as published  by the Free Soft- --
-- ware  Foundation;  either version 2,  or (at your option) any later ver- --
-- sion. GNARL is distributed in the hope that it will be useful, but WITH- --
-- OUT ANY WARRANTY;  without even the  implied warranty of MERCHANTABILITY --
-- or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License --
-- for  more details.  You should have  received  a copy of the GNU General --
-- Public License  distributed with GNARL; see file COPYING.  If not, write --
-- to  the Free Software Foundation,  59 Temple Place - Suite 330,  Boston, --
-- MA 02111-1307, USA.                                                      --
--                                                                          --
-- As a special exception,  if other files  instantiate  generics from this --
-- unit, or you link  this unit with other files  to produce an executable, --
-- this  unit  does not  by itself cause  the resulting  executable  to  be --
-- covered  by the  GNU  General  Public  License.  This exception does not --
-- however invalidate  any other reasons why  the executable file  might be --
-- covered by the  GNU Public License.                                      --
--                                                                          --
-- GNARL was developed by the GNARL team at Florida State University.       --
-- Extensive contributions were provided by Ada Core Technologies, Inc.     --
--                                                                          --
------------------------------------------------------------------------------

--  This is a POSIX version of this package where foreign threads are
--  recognized.

--  Currently, DEC Unix, SCO UnixWare, Solaris pthread, HPUX pthread,
--  GNU/Linux threads, and RTEMS use this version.

separate (System.Task_Primitives.Operations)
package body Specific is

   ----------------
   -- Initialize --
   ----------------

   procedure Initialize (Environment_Task : Task_ID) is
      pragma Warnings (Off, Environment_Task);
      Result : Interfaces.C.int;

   begin
      Result := pthread_key_create (ATCB_Key'Access, null);
      pragma Assert (Result = 0);
   end Initialize;

   -------------------
   -- Is_Valid_Task --
   -------------------

   function Is_Valid_Task return Boolean is
   begin
      return pthread_getspecific (ATCB_Key) /= System.Null_Address;
   end Is_Valid_Task;

   ---------
   -- Set --
   ---------

   procedure Set (Self_Id : Task_ID) is
      Result : Interfaces.C.int;
   begin
      Result := pthread_setspecific (ATCB_Key, To_Address (Self_Id));
      pragma Assert (Result = 0);
   end Set;

   ----------
   -- Self --
   ----------

   --  To make Ada tasks and C threads interoperate better, we have added some
   --  functionality to Self. Suppose a C main program (with threads) calls an
   --  Ada procedure and the Ada procedure calls the tasking runtime system.
   --  Eventually, a call will be made to self. Since the call is not coming
   --  from an Ada task, there will be no corresponding ATCB.

   --  What we do in Self is to catch references that do not come from
   --  recognized Ada tasks, and create an ATCB for the calling thread.

   --  The new ATCB will be "detached" from the normal Ada task master
   --  hierarchy, much like the existing implicitly created signal-server
   --  tasks.

   function Self return Task_ID is
      Result : System.Address;

   begin
      Result := pthread_getspecific (ATCB_Key);

      --  If the key value is Null, then it is a non-Ada task.

      if Result /= System.Null_Address then
         return To_Task_ID (Result);
      else
         return Register_Foreign_Thread;
      end if;
   end Self;

end Specific;
