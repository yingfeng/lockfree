/**
 * Implementation of a Record Manager with several memory reclamation schemes.
 * This file provides mechanisms for printing debugging information.
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

#ifndef DEBUGPRINTING_H
#define	DEBUGPRINTING_H

#include <atomic>
#include <sstream>

#define COUTATOMIC(coutstr) /*cout<<coutstr*/ \
{ \
    stringstream ss; \
    ss<<coutstr; \
    cout<<ss.str(); \
}
#define COUTATOMICTID(coutstr) /*cout<<"tid="<<(tid<10?" ":"")<<tid<<": "<<coutstr*/ \
{ \
    stringstream ss; \
    ss<<"tid="<<tid<<(tid<10?" ":"")<<": "<<coutstr; \
    cout<<ss.str(); \
}

// set __trace to true if you want many paths through the code to be traced with cout<<"..." statements
#ifndef TRACE_DEFINED
std::atomic_bool ___trace(0);
std::atomic_bool ___validateops(0);
#define TRACE_DEFINED
#define TRACE_TOGGLE {bool ___t = ___trace; ___trace = !___t;}
#define TRACE_ON {___trace = true;}
#define TRACE if(___trace)
#endif

#endif	/* DEBUGPRINTING_H */

