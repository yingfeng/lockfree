/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file contains globals used by the Record Manager (mostly for debugging).
 * 
 * Copyright (C) 2016 Trevor Brown
 * Contact (tabrown [at] cs [dot] toronto [dot edu]) with any questions or comments.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RECORDMGR_GLOBALS_H
#define	RECORDMGR_GLOBALS_H

#include "machineconstants.h"
#include "debugprinting.h"

#ifndef DEBUG
#define DEBUG if(0)
#define DEBUG2 if(0)
#endif

#ifndef MEMORY_STATS
#define MEMORY_STATS if(0)
#define MEMORY_STATS2 if(0)
#endif

// don't touch these options for crash recovery

#define CRASH_RECOVERY_USING_SETJMP
#define SEND_CRASH_RECOVERY_SIGNALS
#define AFTER_NEUTRALIZING_SET_BIT_AND_RETURN_TRUE
#define PERFORM_RESTART_IN_SIGHANDLER
#define SIGHANDLER_IDENTIFY_USING_PTHREAD_GETSPECIFIC

// some useful, data structure agnostic definitions

typedef bool CallbackReturn;
typedef void* CallbackArg;
typedef CallbackReturn (*CallbackType)(CallbackArg);
#ifndef SOFTWARE_BARRIER
#define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

#endif	/* GLOBALS_H */
