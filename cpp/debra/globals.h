/**
 * C++ implementation of lock-free chromatic tree using LLX/SCX and DEBRA(+).
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

#ifndef GLOBALS_H
#define	GLOBALS_H

#include <atomic>
#include "recordmgr/machineconstants.h"
#include "debugcounters.h"

#ifndef DEBUG
#define DEBUG if(0)
#define DEBUG1 if(0)
#define DEBUG2 if(0)
#endif

#include "random.h"
static Random rngs[MAX_TID_POW2*PREFETCH_SIZE_WORDS]; // create per-thread random number generators (padded to avoid false sharing)
extern Random rngs[MAX_TID_POW2*PREFETCH_SIZE_WORDS];

#define HAS_CPU_SETS

// some useful options for the chromatic tree

//#define NOREBALANCING

#endif	/* GLOBALS_H */